# 微信小程序

用户端微信小程序，提供书架浏览、扫码借还、个人管理等功能。

## 技术栈

- 微信小程序原生框架
- Vant Weapp 组件库
- 直连 RK3566 主控 HTTP 服务

## 目录结构

```
├── app.js / app.json / app.wxss    小程序入口与全局配置
├── config/
│   └── api.js                      API 地址配置
├── model/
│   └── book.js                     书本数据模型
├── utils/
│   ├── request.js                  HTTP 请求封装
│   ├── util.js                     工具函数
│   └── qrcode.js                   QR 码生成
├── components/
│   └── shelf-grid/                 书架网格组件
├── pages/
│   ├── shelf/                      书架浏览（Tab 1）
│   ├── borrow/                     借还（Tab 2）
│   ├── my/                         我的（Tab 3）
│   ├── book-detail/                书本详情
│   ├── qr-show/                    QR 码展示（借/还任务码）
│   ├── return/                     还书流程
│   ├── search/                     搜索书籍
│   ├── index/                      入口/登录
│   ├── admin/                      管理员面板
│   ├── entry/                      语音录入
│   ├── inventory/                  库存管理
│   ├── scan/                       扫码页
│   ├── logs/                       操作日志
│   └── mine/                       个人信息
└── image/                          图标资源
```

## 3 Tab 架构

| Tab | 页面 | 功能 |
|-----|------|------|
| 书架 | `pages/shelf/shelf` | 16 格书架实时状态、分区浏览 |
| 借还 | `pages/borrow/borrow` | 扫码借书/还书、任务 QR 生成 |
| 我的 | `pages/my/my` | 借阅记录、逾期状态、PIN 提权 |

## 开发环境

1. 安装[微信开发者工具](https://developers.weixin.qq.com/miniprogram/dev/devtools/download.html)
2. 导入项目目录
3. 在 `config/api.js` 中配置后端 API 地址
4. 在微信后台配置 request 合法域名（HTTPS 必需）

## 交互流程

### 借书
```
浏览书架 → 选书 → 点"借这本书" → 生成任务 QR → 出示给 OV5695 摄像头扫码
→ 机械爪取书 → 轮询完成 → 用户取书
```

### 还书
```
点"我要还书" → 生成还书 QR → 把书放到整书区 → 出示 QR 扫码
→ 系统自动推书→扫码→上架 → 轮询完成
```

## 相关文档

- 部署指南：`../7-项目文档/部署指南.md`
- 通信协议：`../7-项目文档/通信协议规范.md`
