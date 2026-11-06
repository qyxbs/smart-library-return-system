#!/usr/bin/env python3
"""
Qwen3-0.6B LoRA 微调 — 书名提取+分类 (四新分类: 教辅/文学/科技/其他)
运行环境: ModelScope A10 GPU (24GB VRAM)
预计时间: 20-40min

用法:
  1. 上传此脚本 + book_extract_train.jsonl 到 ModelScope Notebook
  2. pip install -q unsloth transformers datasets accelerate peft bitsandbytes trl
  3. python3 finetune_modelscope.py
"""

import json, os, glob
from collections import Counter

# ===== 配置 =====
TRAIN_FILE = "book_extract_train.jsonl"       # 训练数据
OUTPUT_ADAPTER = "qwen3-book-v2-lora"          # LoRA adapter 输出
OUTPUT_MERGED = "qwen3-book-v2-merged"         # 合并模型输出
OUTPUT_GGUF = "qwen3-book-v2-gguf"             # GGUF 输出
QUANT_METHOD = "q4_k_m"                        # 量化方式

# 新四分类
CATEGORIES = ["教辅", "文学", "科技", "其他"]
SYSTEM_PROMPT = (
    "你是书籍提取助手。从语音文本提取书名和分类，输出格式：书名|分类。"
    "分类只有4种："
    "- 教辅：教材/考试/真题/辅导/启蒙/绘本"
    "- 文学：小说/诗歌/散文/科幻/漫画/轻小说"
    "- 科技：编程/科普/医学/工程/自然科学"
    "- 其他：历史/哲学/经济/艺术/生活/心理/励志等"
    "忽略口语中的干扰词。只输出一行结果，不要解释。"
)

# ===== 加载训练数据 =====
print("=" * 50)
print("1. 加载训练数据")
data = []
with open(TRAIN_FILE, encoding='utf-8') as f:
    for line in f:
        line = line.strip()
        if line and '"messages"' in line:
            data.append(json.loads(line))
print(f"   训练数据: {len(data)} 条")

# 统计分类分布
cat_counts = Counter()
for d in data:
    msgs = d['messages']
    last = msgs[-1]['content']
    if '|' in last:
        cat = last.rsplit('|', 1)[-1].strip()
        cat_counts[cat] += 1
print(f"   分类分布:")
for cat, n in cat_counts.most_common():
    print(f"     {cat}: {n} ({100*n/max(1,len(data)):.1f}%)")

# ===== 加载模型 =====
print("\n2. 加载 Qwen3-0.6B (Unsloth 4-bit)")
from unsloth import FastLanguageModel
import torch

max_seq_length = 512

model, tokenizer = FastLanguageModel.from_pretrained(
    model_name="unsloth/Qwen3-0.6B-unsloth-bnb-4bit",
    max_seq_length=max_seq_length,
    dtype=None,
    load_in_4bit=True,
)
print(f"   模型加载完成")

# ===== 配置 LoRA =====
print("\n3. 配置 LoRA")
model = FastLanguageModel.get_peft_model(
    model,
    r=16,
    lora_alpha=32,
    lora_dropout=0.05,
    target_modules=["q_proj", "k_proj", "v_proj", "o_proj",
                    "gate_proj", "up_proj", "down_proj"],
    bias="none",
    use_gradient_checkpointing="unsloth",
    random_state=42,
)
trainable = sum(p.numel() for p in model.parameters() if p.requires_grad)
total = sum(p.numel() for p in model.parameters())
print(f"   可训练: {trainable/1e6:.1f}M / {total/1e6:.1f}M ({100*trainable/max(1,total):.1f}%)")

# ===== 格式化数据集 =====
print("\n4. 格式化数据集")
from datasets import Dataset

dataset = Dataset.from_list(data)

def format_func(examples):
    texts = []
    for msgs in examples['messages']:
        text = tokenizer.apply_chat_template(
            msgs, tokenize=False, add_generation_prompt=False)
        texts.append(text)
    return {'text': texts}

dataset = dataset.map(format_func, batched=True)
print(f"   数据集: {len(dataset)} 条")

# ===== 训练 =====
print("\n5. 开始训练...")
from trl import SFTTrainer
from transformers import TrainingArguments

trainer = SFTTrainer(
    model=model,
    tokenizer=tokenizer,
    train_dataset=dataset,
    dataset_text_field="text",
    max_seq_length=max_seq_length,
    args=TrainingArguments(
        per_device_train_batch_size=8,
        gradient_accumulation_steps=2,
        num_train_epochs=3,
        learning_rate=2e-4,
        fp16=not torch.cuda.is_bf16_supported(),
        bf16=torch.cuda.is_bf16_supported(),
        logging_steps=50,
        save_steps=500,
        output_dir="./checkpoints",
        optim="adamw_8bit",
        warmup_ratio=0.05,
        lr_scheduler_type="cosine",
        seed=42,
    ),
)

trainer.train()
print("   训练完成!")

# ===== 测试推理 =====
print("\n6. 测试推理 (无思考模式)")
FastLanguageModel.for_inference(model)

test_cases = [
    ("额那个三体", "文学"),
    ("雅思写作", "教辅"),
    ("Python编程从入门到实践", "科技"),
    ("活着", "文学"),
    ("五年高考三年模拟", "教辅"),
    ("人类简史", "其他"),
    ("小王子", "文学"),
    ("时间简史", "科技"),
]

ok = 0
for inp, expected in test_cases:
    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": f"语音：{inp}"},
    ]
    inputs = tokenizer.apply_chat_template(
        messages, tokenize=True, add_generation_prompt=True, return_tensors="pt")
    inputs = inputs.to("cuda")

    outputs = model.generate(
        inputs, max_new_tokens=20, temperature=0, do_sample=False)

    result = tokenizer.decode(outputs[0][len(inputs[0]):], skip_special_tokens=True).strip()
    # 去掉 think 标签
    import re
    result = re.sub(r'<think>.*?</think>\s*', '', result, flags=re.DOTALL).strip()
    cat_match = result.rsplit('|', 1)[-1].strip() if '|' in result else ''
    status = 'OK' if expected in cat_match else f'WRONG(expected {expected})'
    if expected in cat_match:
        ok += 1
    print(f"   [{status}] {inp} → {result}")

print(f"\n   {ok}/{len(test_cases)} 准确")

# ===== 保存 LoRA =====
print("\n7. 保存 LoRA adapter")
model.save_pretrained(OUTPUT_ADAPTER)
tokenizer.save_pretrained(OUTPUT_ADAPTER)
print(f"   已保存: {OUTPUT_ADAPTER}/")

# ===== 合并 + 导出 GGUF =====
print(f"\n8. 导出 GGUF {QUANT_METHOD}")
model.save_pretrained_merged(OUTPUT_MERGED, tokenizer, save_method="merged_16bit")
model.save_pretrained_gguf(OUTPUT_GGUF, tokenizer, quantization_method=QUANT_METHOD)

for f in glob.glob(f'{OUTPUT_GGUF}/*.gguf'):
    size_mb = os.path.getsize(f) / 1e6
    print(f"   {f} ({size_mb:.0f}MB)")

print("\n" + "=" * 50)
print("完成! 下载 GGUF 文件部署到 RK3566:")
print(f"  scp {OUTPUT_GGUF}/*.gguf root@<BOARD_IP>:/userdata/voice_app/models/")
