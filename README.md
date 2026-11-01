# FortressFinderBase

下界要塞搜索核心。本仓库拥有全部 C++ 计算代码；
[cubiomes](https://github.com/xpple/cubiomes) 作为本仓库的嵌套 git 子模块放在
`cubiomes/`，因此可以独立构建，不依赖 Java 模块的源码树。

## 产物与契约

| 产物 | 路径 | 用途 |
| --- | --- | --- |
| 静态引擎库 | `build` 中的 `fortressfinder-core` | CLI 与 JNI 共用的 C++ 引擎 |
| 独立 CLI | `build/cli/fortressfinder-cli.exe` | 纯命令行搜索，不依赖 Java / DLL |
| JNI 动态库 | `build/native/fortressFinderLibJ.dll` | 供 FortressFinder Java GUI 查询 |

JNI 契约（`FortressFinderBridge`）保持与旧版完全一致：

- `fortressSearch(...)`：cross 模式每命中 6 个 int，span 模式每命中 8 个 int。
- `getSearchProgress()`：返回 `[current, total, phase1, status]`。
- `getNowResult()`、`pause()`、`resume()`、`stop()`、`resetSearchState()`。

`jni/generated/sunnyslopes_fortressfinder_FortressFinderBridge.h` 是签入的
JNI 头文件，使本仓库不依赖 Java 源码即可构建。修改 `FortressFinderBridge.java`
后需要重新生成并同步该头文件；主模块的 `verifyJniContract` Gradle 任务会检查
两边是否一致。

## 子模块与构建

首次克隆后初始化嵌套子模块：

```powershell
git submodule update --init --recursive
```

### 独立 CLI（推荐）

```powershell
powershell -ExecutionPolicy Bypass -File build_cli_msys2.ps1
.\build\cli\fortressfinder-cli.exe --help
```

也可以使用 CMake：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\bin\fortressfinder-cli.exe --help
```

MinGW 下 exe 静态链接 cubiomes 和 C++ 运行库，可以直接复制到其他 Windows
机器运行。

### JNI 库

```powershell
powershell -ExecutionPolicy Bypass -File build_jni_msys2.ps1
```

产物为 `build/native/fortressFinderLibJ.dll`。也可以使用 CMake：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target fortressFinderLibJ
```

## 命令行用法

```powershell
# cross 模式，搜索半径 5000，导出 txt
.\build\cli\fortressfinder-cli.exe --seed 12345 --mode cross --radius 5000 --out cross.txt

# span 模式，按长边/短边/高度筛选
.\build\cli\fortressfinder-cli.exe --seed 12345 --mode span --min-long 100 --min-short 60 `
    --min-height 20 --center-x 1000 --center-z -1000 --radius 8000 --out span.txt

# 显式矩形区域
.\build\cli\fortressfinder-cli.exe --seed 12345 --mode cross --min-x -10000 --max-x 10000 `
    --min-z -10000 --max-z 10000 --out cross_rect.txt
```

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--seed <int64>` | 必填 | 世界种子，可为负数 |
| `--mode cross\|span` | `cross` | 十字路口 / 大范围要塞 |
| `--mc <id\|version>` | `26.3` | 版本名或 cubiomes `MCVersion` 数值（19–35），见下方版本分组 |
| `--center-x` / `--center-z` | `0` | 搜索中心 |
| `--radius` | `2000` | 搜索半径（方块） |
| `--min-x/--max-x/--min-z/--max-z` | 未设置 | 显式矩形，四项必须一起给出，优先于 center/radius |
| `--cross-filter all\|triple-quad\|quad` | `all` | cross 模式过滤 |
| `--min-long` | `0` | span 模式长边下限 |
| `--min-short` | `0` | span 模式短边下限；0 表示沿用 `--min-long` |
| `--min-height` | `0` | span 模式 Y 高度差下限 |
| `--threads` | CPU 数 | 1–256 |
| `--limit` | `0` | 最多输出条数；0 表示全部 |
| `--out <file>` | 未设置 | 同时把同一份文本报告写入 txt（UTF-8） |
| `--help` / `-h` | — | 显示帮助 |
| `--version` | — | 显示版本 |

文本报告同时打印到 stdout；指定 `--out` 时再写入文件。cross 模式列：

```text
chunkX  chunkZ  x  y  z  crossings  shape  teleport
```

span 模式列：

```text
chunkX  chunkZ  x  y  z  spanX  spanY  spanZ  teleport
```

结果按距原点距离、X、Z 排序，与 Java GUI 的展示顺序一致。

## 版本分组

下界要塞生成按版本分为两组，组内所有补丁版本生成结果完全一致：

| 分组 | 搜索使用的地图 | 版本别名 |
| --- | --- | --- |
| `1.18 ~ 26.3+` | 26.3（`MC_26_3`） | 1.18、1.18.1、1.18.2、1.19、1.19.2、1.19.4、1.20、1.20.1、1.20.4、1.20.6、1.21、1.21.1、1.21.3、1.21.4、1.21.5、1.21.6、1.21.9、1.21.11、26.1、26.2、26.3、latest |
| `1.16 ~ 1.17` | 1.17（`MC_1_17_1`） | 1.16、1.16.1、1.16.5、1.17、1.17.1 |

1.16~1.17 与 1.18+ 的要塞位置、可生成性均不同，因此保留独立分组；
1.18 与 26.3 之间只存在与要塞无关的生成变化（例如 26.3 的堡垒结构盐），
要塞位置、可生成性、完整结构片段完全相同。

可以用 `fortress-version-check` 复核这个结论（见下节）。

## 版本一致性检查

`tools/fortress_version_check.cpp` 会在指定种子与结构区域网格上，
逐区域比较各版本的：要塞结构位置、`isViableStructurePos`、堡垒位置/可生成性、
堡垒变体以及每个要塞片段的类型与包围盒。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFORTRESSFINDER_BUILD_JNI=OFF
cmake --build build --target fortress-version-check
.\build\bin\fortress-version-check.exe --versions 22,24,25,26,33,34,35 --regions 200
```

默认以 1.18.2 为基线、检查 5 个种子、半径 40 个结构区域。退出码非 0 表示
存在版本差异；加 `--report-only` 只报告不失败（用于确认 1.16/1.17 确实
与 1.18+ 不同）。

## 目录

```text
fortress_finder.h / .cpp   C++ 引擎与公开 API
fortress_cli.cpp           独立 CLI
Thread.h                   线程池与线程安全结果容器
jni/                       JNI 桥接与签入头文件
tools/                     开发工具（fortress-version-check）
cubiomes/                  嵌套 git 子模块
build_jni_msys2.ps1        MSYS2 构建 JNI DLL
build_cli_msys2.ps1        MSYS2 构建独立 exe
```
