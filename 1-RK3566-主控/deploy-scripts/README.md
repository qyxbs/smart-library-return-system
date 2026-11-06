# 部署脚本

| 脚本 | 说明 |
|---|---|
| `deploy_cpp.sh` | 交叉编译 C++ 主控并部署到板端（book_server / k230_bridge / static / init 脚本） |
| `archive/deploy_full_amp.sh` | 历史脚本：AMP + PDM 内核模块部署（与当前主控无关，仅存档） |

前置：交叉编译工具链 + `cpp/sysroot-arm64`（`cpp/scripts/fetch_sysroot.sh` 从板端提取）。
