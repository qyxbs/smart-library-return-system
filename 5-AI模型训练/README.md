# AI 模型训练

本项目使用 AI 模型实现语音识别（ASR）、声纹验证、书名提取与自动分类。

## 模型架构

| 环节 | 模型 | 运行位置 | 说明 |
|------|------|---------|------|
| 声纹验证 | ERes2Net (sherpa-onnx) | RK3566 CPU | 512 维声纹嵌入，余弦相似度 ≥0.5 |
| 语音识别 (ASR) | SenseVoice-Small int8 | RK3566 CPU | 229MB，RTF≈0.6 |
| 书名提取+分类 | Qwen3-0.6B LoRA 微调 | RK3566 CPU (本地) | Q4_K_M 量化 379MB，8.25 tok/s |
| 书名提取+分类 | DeepSeek API | 云端 | 网络正常时优先使用 |

## 模型训练

### 训练数据

使用 DeepSeek API 全合成 15,144 条训练数据，覆盖四大分类（教辅/文学/科技/其他），各分类占比均衡（25%±1%）。

数据包含 100+ 种真实场景噪声组合：
- 同音字替换 (22%)
- 前缀噪音 (16%)："额那个"、"我想找"等口语化前缀
- 后缀噪音 (12%)
- 混合噪音 (13%)

### 微调

在 ModelScope A10 GPU 上使用 LoRA 微调 Qwen3-0.6B：
- 3 epochs，final loss 0.20
- 分类准确率 80%（基座模型 30%）
- 合并适配器 → FP16 → llama.cpp Q4_K_M 量化 (610MB → 379MB)

### 推理部署

RK3566 上使用 llama.cpp (ARM 交叉编译，DOTPROD+FP16_VA)：
- 持久 llama-server + GBNF 语法约束
- `cache_prompt: true` 热推理 ~3s
- 云端 API 不可用时自动切换本地模型

## 文件说明

训练相关脚本与数据位于项目仓库的 `rk3566/book_data/` 路径下（因文件较大，需单独获取）：

| 文件 | 说明 |
|------|------|
| `finetune_modelscope.py` | ModelScope LoRA 微调脚本 |
| `finetune_colab.ipynb` | Google Colab 微调笔记本 |
| `book_extract_train_v2_clean.jsonl` | 训练数据（15,144 条） |
| `lightweight_models/train_and_validate.py` | 轻量模型训练验证 |

## 相关文档

- 系统架构中的 AI 管线：`../7-项目文档/系统架构说明.md`
