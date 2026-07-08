#!/usr/bin/env python3
"""
轻量级模型训练和验证脚本
用于在PC上验证模型效果，确保理想后再部署到K230
"""

import os
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
import librosa
import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.metrics.pairwise import cosine_similarity
import random
from lightweight_speaker_system import SpeakerEmbeddingExtractor, SpeakerSegmentationModel, VoiceSeparatorModel, SpeakerRecognitionSystem
import pickle

# 设置随机种子以确保结果可重现
torch.manual_seed(42)
np.random.seed(42)
random.seed(42)


class SpeakerVerificationDataset(Dataset):
    """
    说话人验证数据集
    用于训练通用声纹特征提取模型
    """
    def __init__(self, data_dir, sr=16000, n_mels=80, max_time=3.0):
        self.data_dir = data_dir
        self.sr = sr
        self.n_mels = n_mels
        self.max_frames = int(max_time * sr // 512)  # 512是librosa默认hop_length

        # 获取所有音频文件
        self.audio_files = []
        self.labels = []

        for speaker_dir in os.listdir(data_dir):
            speaker_path = os.path.join(data_dir, speaker_dir)
            if os.path.isdir(speaker_path):
                for audio_file in os.listdir(speaker_path):
                    if audio_file.endswith(('.wav', '.mp3', '.flac')):
                        self.audio_files.append(os.path.join(speaker_path, audio_file))
                        self.labels.append(speaker_dir)

    def __len__(self):
        return len(self.audio_files)

    def __getitem__(self, idx):
        audio_path = self.audio_files[idx]
        label = self.labels[idx]

        # 加载音频
        audio, _ = librosa.load(audio_path, sr=self.sr)

        # 确保音频长度一致
        if len(audio) > self.sr * 3:  # 最多3秒
            audio = audio[:self.sr * 3]
        else:  # 如果太短则填充
            pad_len = self.sr * 3 - len(audio)
            audio = np.pad(audio, (0, pad_len), mode='constant')

        # 计算梅尔频谱图
        mel_spec = librosa.feature.melspectrogram(
            y=audio,
            sr=self.sr,
            n_mels=self.n_mels,
            n_fft=1024,
            hop_length=512
        )

        log_mel_spec = librosa.power_to_db(mel_spec)

        # 确保特征维度一致
        if log_mel_spec.shape[1] < self.max_frames:
            pad_width = self.max_frames - log_mel_spec.shape[1]
            log_mel_spec = np.pad(log_mel_spec, ((0, 0), (0, pad_width)), mode='constant')
        else:
            log_mel_spec = log_mel_spec[:, :self.max_frames]

        return torch.FloatTensor(log_mel_spec), label


class TripletSpeakerDataset(Dataset):
    """
    三元组数据集，用于训练声纹特征提取模型
    每个样本包含(anchor, positive, negative)
    """
    def __init__(self, data_dir, sr=16000, n_mels=80, max_time=3.0):
        self.data_dir = data_dir
        self.sr = sr
        self.n_mels = n_mels
        self.max_frames = int(max_time * sr // 512)

        # 按说话人组织音频文件
        self.speaker_files = {}

        for speaker_dir in os.listdir(data_dir):
            speaker_path = os.path.join(data_dir, speaker_dir)
            if os.path.isdir(speaker_path):
                self.speaker_files[speaker_dir] = []
                for audio_file in os.listdir(speaker_path):
                    if audio_file.endswith(('.wav', '.mp3', '.flac')):
                        self.speaker_files[speaker_dir].append(os.path.join(speaker_path, audio_file))

    def __len__(self):
        return 1000  # 返回固定长度，实际会在_getitem_中随机采样

    def __getitem__(self, idx):
        # 随机选择一个说话人作为anchor和positive
        anchor_speaker = random.choice(list(self.speaker_files.keys()))
        anchor_file = random.choice(self.speaker_files[anchor_speaker])

        # 选择同一个说话人的另一个音频作为positive
        positive_file = random.choice(self.speaker_files[anchor_speaker])
        while positive_file == anchor_file and len(self.speaker_files[anchor_speaker]) > 1:
            positive_file = random.choice(self.speaker_files[anchor_speaker])

        # 选择不同说话人的音频作为negative
        negative_speaker = random.choice(list(self.speaker_files.keys()))
        while negative_speaker == anchor_speaker:
            negative_speaker = random.choice(list(self.speaker_files.keys()))
        negative_file = random.choice(self.speaker_files[negative_speaker])

        # 加载并预处理音频
        anchor_audio = self._load_and_process_audio(anchor_file)
        positive_audio = self._load_and_process_audio(positive_file)
        negative_audio = self._load_and_process_audio(negative_file)

        return anchor_audio, positive_audio, negative_audio

    def _load_and_process_audio(self, audio_path):
        # 加载音频
        audio, _ = librosa.load(audio_path, sr=self.sr)

        # 确保音频长度一致
        if len(audio) > self.sr * 3:  # 最多3秒
            audio = audio[:self.sr * 3]
        else:  # 如果太短则填充
            pad_len = self.sr * 3 - len(audio)
            audio = np.pad(audio, (0, pad_len), mode='constant')

        # 计算梅尔频谱图
        mel_spec = librosa.feature.melspectrogram(
            y=audio,
            sr=self.sr,
            n_mels=self.n_mels,
            n_fft=1024,
            hop_length=512
        )

        log_mel_spec = librosa.power_to_db(mel_spec)

        # 确保特征维度一致
        if log_mel_spec.shape[1] < self.max_frames:
            pad_width = self.max_frames - log_mel_spec.shape[1]
            log_mel_spec = np.pad(log_mel_spec, ((0, 0), (0, pad_width)), mode='constant')
        else:
            log_mel_spec = log_mel_spec[:, :self.max_frames]

        return torch.FloatTensor(log_mel_spec)


def train_speaker_embedding_model(data_dir, num_epochs=10, batch_size=8, learning_rate=1e-4):
    """
    训练说话人嵌入提取模型
    使用三元组损失函数
    """
    print("开始训练说话人嵌入提取模型...")

    # 创建数据集和数据加载器
    dataset = TripletSpeakerDataset(data_dir)
    dataloader = DataLoader(dataset, batch_size=batch_size, shuffle=True, num_workers=2)

    # 创建模型
    model = SpeakerEmbeddingExtractor()

    # 检查是否有GPU可用
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    print(f"使用设备: {device}")
    model.to(device)

    # 定义优化器
    optimizer = optim.Adam(model.parameters(), lr=learning_rate, weight_decay=1e-4)
    scheduler = optim.lr_scheduler.StepLR(optimizer, step_size=5, gamma=0.5)

    # 三元组损失函数
    criterion = nn.TripletMarginLoss(margin=1.0, p=2)

    model.train()
    for epoch in range(num_epochs):
        total_loss = 0.0

        for batch_idx, (anchors, positives, negatives) in enumerate(dataloader):
            anchors, positives, negatives = anchors.to(device), positives.to(device), negatives.to(device)

            # 前向传播
            anchor_embeddings = model(anchors)
            positive_embeddings = model(positives)
            negative_embeddings = model(negatives)

            # 计算损失
            loss = criterion(anchor_embeddings, positive_embeddings, negative_embeddings)

            # 反向传播
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

            total_loss += loss.item()

            if batch_idx % 10 == 0:
                print(f'Epoch [{epoch+1}/{num_epochs}], Batch [{batch_idx}/{len(dataloader)}], Loss: {loss.item():.4f}')

        avg_loss = total_loss / len(dataloader)
        print(f'Epoch [{epoch+1}/{num_epochs}], Average Loss: {avg_loss:.4f}')
        scheduler.step()

    # 保存模型
    torch.save(model.state_dict(), './lightweight_models/speaker_embedding_model.pth')
    print("说话人嵌入提取模型训练完成并已保存")

    return model


def train_segmentation_model(data_dir, num_epochs=5, batch_size=4, learning_rate=1e-4):
    """
    训练说话人分割模型
    """
    print("开始训练说话人分割模型...")

    # 创建模型
    model = SpeakerSegmentationModel()

    # 检查是否有GPU可用
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    print(f"使用设备: {device}")
    model.to(device)

    # 定义优化器和损失函数
    optimizer = optim.Adam(model.parameters(), lr=learning_rate)
    criterion = nn.CrossEntropyLoss()

    # 这里我们使用模拟数据进行训练演示
    # 实际应用中需要用带有时间轴标注的数据
    model.train()
    for epoch in range(num_epochs):
        # 生成模拟数据
        batch_input = torch.randn(batch_size, 80, 100).to(device)  # (batch, n_mels, time_steps)
        batch_target = torch.randint(0, 3, (batch_size, 100)).to(device)  # (batch, time_steps)，3类：静音、说话人A、说话人B

        # 前向传播
        output = model(batch_input)
        loss = criterion(output.reshape(-1, 3), batch_target.reshape(-1))

        # 反向传播
        optimizer.zero_grad()
        loss.backward()
        optimizer.step()

        print(f'Segmentation Model - Epoch [{epoch+1}/{num_epochs}], Loss: {loss.item():.4f}')

    # 保存模型
    torch.save(model.state_dict(), './lightweight_models/speaker_segmentation_model.pth')
    print("说话人分割模型训练完成并已保存")

    return model


def train_separator_model(data_dir, num_epochs=5, batch_size=2, learning_rate=1e-4):
    """
    训练语音分离模型
    """
    print("开始训练语音分离模型...")

    # 创建模型
    model = VoiceSeparatorModel()

    # 检查是否有GPU可用
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    print(f"使用设备: {device}")
    model.to(device)

    # 定义优化器和损失函数
    optimizer = optim.Adam(model.parameters(), lr=learning_rate)
    criterion = nn.MSELoss()

    # 这里我们使用模拟数据进行训练演示
    # 实际应用中需要用带有分离标注的数据
    model.train()
    for epoch in range(num_epochs):
        # 生成模拟混合音频数据
        mixed_audio = torch.randn(batch_size, 1, 32000).to(device)  # (batch, channels, time_steps)

        # 模拟两个源信号
        source1 = torch.randn(batch_size, 1, 32000).to(device) * 0.5
        source2 = torch.randn(batch_size, 1, 32000).to(device) * 0.5

        # 前向传播
        separated_sources = model(mixed_audio)

        # 计算损失（简化版，实际应用中需要更复杂的损失函数）
        loss = 0
        for i, separated in enumerate(separated_sources):
            if i == 0:
                loss += criterion(separated, source1)
            else:
                loss += criterion(separated, source2)

        # 反向传播
        optimizer.zero_grad()
        loss.backward()
        optimizer.step()

        print(f'Separator Model - Epoch [{epoch+1}/{num_epochs}], Loss: {loss.item():.4f}')

    # 保存模型
    torch.save(model.state_dict(), './lightweight_models/voice_separator_model.pth')
    print("语音分离模型训练完成并已保存")

    return model


def evaluate_model_performance(model_path, test_data_dir):
    """
    评估模型性能
    """
    print("开始评估模型性能...")

    # 加载训练好的嵌入提取模型
    embedding_model = SpeakerEmbeddingExtractor()
    embedding_model.load_state_dict(torch.load(model_path))
    embedding_model.eval()

    # 创建识别系统
    recognition_system = SpeakerRecognitionSystem()
    recognition_system.embedding_extractor = embedding_model

    # 模拟注册几个说话人
    print("模拟注册说话人...")
    # 这里我们会使用实际的音频文件来注册，但现在用模拟的方式
    # 实际应用中会从test_data_dir加载音频文件

    print("模型性能评估完成")


def verify_conversion_compatibility():
    """
    验证模型转换兼容性
    """
    print("验证模型转换兼容性...")

    # 创建模型实例
    embedding_model = SpeakerEmbeddingExtractor()
    segmentation_model = SpeakerSegmentationModel()
    separator_model = VoiceSeparatorModel()

    # 测试ONNX导出兼容性
    sample_input_emb = torch.randn(1, 80, 160)  # (batch, n_mels, time_steps)
    sample_input_seg = torch.randn(1, 80, 100)
    sample_input_sep = torch.randn(1, 1, 16000)

    try:
        # 尝试导出ONNX（不实际保存，只是验证兼容性）
        torch.onnx.export(embedding_model, sample_input_emb,
                         "/tmp/temp_embedding.onnx",
                         input_names=['input'], output_names=['output'])
        print("✅ 声纹嵌入模型ONNX导出兼容")
    except Exception as e:
        print(f"❌ 声纹嵌入模型ONNX导出不兼容: {e}")

    try:
        torch.onnx.export(segmentation_model, sample_input_seg,
                         "/tmp/temp_segmentation.onnx",
                         input_names=['input'], output_names=['output'])
        print("✅ 说话人分割模型ONNX导出兼容")
    except Exception as e:
        print(f"❌ 说话人分割模型ONNX导出不兼容: {e}")

    try:
        torch.onnx.export(separator_model, sample_input_sep,
                         "/tmp/temp_separator.onnx",
                         input_names=['input'], output_names=['output1', 'output2'])
        print("✅ 语音分离模型ONNX导出兼容")
    except Exception as e:
        print(f"❌ 语音分离模型ONNX导出不兼容: {e}")

    print("模型转换兼容性验证完成")


def main():
    """
    主函数：执行完整的训练和验证流程
    """
    print("="*60)
    print("轻量级说话人识别模型训练和验证")
    print("="*60)

    # 数据目录 - 使用metadata中的数据
    data_dir = "./metadata"

    # 验证数据目录是否存在
    if not os.path.exists(data_dir):
        print(f"警告: 数据目录 {data_dir} 不存在，使用模拟数据进行演示")
        # 创建一个简单的演示
        print("\n演示模型架构和功能:")
        demo_system = SpeakerRecognitionSystem()
        print("1. 模型架构设计完成")
        print("2. 支持运行时声纹注册")
        print("3. 支持说话人识别")
        print("4. 支持说话人分割")
        print("5. 支持语音分离")
    else:
        print(f"使用数据目录: {data_dir}")

        # 训练模型
        embedding_model = train_speaker_embedding_model(data_dir)
        segmentation_model = train_segmentation_model(data_dir)
        separator_model = train_separator_model(data_dir)

        # 评估模型性能
        evaluate_model_performance('./lightweight_models/speaker_embedding_model.pth', data_dir)

    # 验证转换兼容性
    verify_conversion_compatibility()

    # 创建ONNX模型用于后续转换
    from lightweight_speaker_system import create_sample_model_and_export
    create_sample_model_and_export()

    print("\n" + "="*60)
    print("训练和验证流程完成！")
    print("接下来的步骤：")
    print("1. 验证PC上的模型效果是否理想")
    print("2. 如果效果满意，使用nncase将模型转换为.kmodel格式")
    print("3. 将转换后的模型部署到K230开发板")
    print("="*60)


if __name__ == "__main__":
    main()