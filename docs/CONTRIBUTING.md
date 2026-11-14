# 贡献指南

## 提交信息规范

本项目采用 [Conventional Commits 1.0.0](https://www.conventionalcommits.org/zh-hans/)。

```
<type>(<scope>): <subject>

<body>

<footer>
```

### type

| 类型 | 用途 |
| --- | --- |
| `feat` | 新功能 |
| `fix` | 修复缺陷 |
| `docs` | 文档变更 |
| `style` | 格式调整（不影响逻辑） |
| `refactor` | 重构（既不新增功能也不修缺陷） |
| `perf` | 性能优化 |
| `test` | 测试相关 |
| `build` | 构建系统或依赖变更 |
| `ci` | 持续集成配置 |
| `chore` | 杂项（版本号、脚手架等） |
| `revert` | 回滚提交 |

### scope（可选）

建议使用模块名：`core`、`sim`、`ui`、`docs`、`build`、`test`、`packaging`。

### 书写要求

- `subject` 使用祈使句、简洁明确，不超过 50 个字符，结尾不加句号
- `body` 说明**为什么**修改以及影响范围，可分行列举
- 破坏性变更在 type 后加 `!`，或在 footer 写 `BREAKING CHANGE: 说明`

### 示例

```
feat(sim): 支持自适应步长瞬态分析
fix(ui): 修复旋转后引脚标签与符号重叠
refactor(core): 用并查集替换网表连通性遍历
perf(ui): 仅在需要时重算节点编号
docs: 补充 MNA 装配公式与验证数据
test(core): 增加二极管整流基准用例
build: 将 Qt 最低版本提升到 6.5
chore: 发布 0.2.0
```

破坏性变更：

```
feat(core)!: 重命名网表 API 为节点索引

BREAKING CHANGE: Netlist::nodeOf 参数由引脚指针改为 (componentId, pinIndex)。
```

## 开发流程

1. 从 `master` 创建分支，例如 `feat/adaptive-step`、`fix/wire-snap`
2. 修改后本地验证：

   ```bash
   cmake --build build -j"$(nproc)"
   ctest --test-dir build --output-on-failure
   ```

3. 按上述规范提交，一个提交只做一件事
4. 合并前确保所有测试通过、无编译警告

## 代码风格

- C++17，4 空格缩进，类型使用 `PascalCase`，成员使用 `m_` 前缀
- 头文件使用 `#pragma once`
- 界面文本使用中文，注释解释“为什么”而非“是什么”
- 新增元件需同时补充：符号绘制（`Symbols.cpp`）、求解器装配（`Simulator.cpp`）、属性面板与测试
