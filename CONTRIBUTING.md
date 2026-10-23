# 协作指南（CONTRIBUTING）

给一起改这个项目的同学看的。第一次参与请从上往下读完，大约 5 分钟。

---

## 1. 环境（版本必须和下面一致）

| 项 | 版本 | 说明 |
|---|---|---|
| AURIX Development Studio | **1.10.40** | 版本不一致时 `.cproject` 可能打不开或编译报错 |
| 主控板 | TC264 主控板 V1.2 | 引脚定义按这块板的丝印 |
| 底层库 | 逐飞 TC264 开源库 | 已随每个工程放在 `libraries/`，**不要改** |
| Git | 任意较新版本 | 命令行或 GitHub Desktop 都行 |

下载安装 ADS 后不需要额外装编译器，里面自带 TASKING 工具链。

---

## 2. 拿到代码

```bash
# 第一次（约 140 MB，耐心等一下）
git clone https://github.com/xiebobo-star/111-dianqi-meinusi-zhinengche.git
cd 111-dianqi-meinusi-zhinengche
```

### 先配好你自己的 git 身份（很重要）

不配的话提交记录会挂在占位的 `Demo` 名下，看不出是谁改的：

```bash
git config --global user.name "你的GitHub用户名"
git config --global user.email "你的GitHub邮箱"
```

### 编辑器编码必须设成 GBK

所有 `.c/.h` 都是 **GBK 编码**（ADS 在中文 Windows 下的默认编码）。

- **VS Code**：设置里搜 `files.encoding` 改成 `gbk`
- **Keil / ADS**：默认就是 GBK，不用改

⚠️ 用 UTF-8 打开再保存，中文注释会变成乱码，**而 TASKING 编译器遇到非法字符会直接报错编不过**。改之前先确认编码。

---

## 3. 分工：一人一个目录

8 个工程是**相互独立**的 ADS 工程，各自有自己的 `.cproject` 和 `libraries`，所以最省事的办法就是**各改各的目录**：

| 目录 | 内容 | 负责人 |
|---|---|---|
| `camera` | 摄像头采集与显示、曝光/阈值调节 | 待分配 |
| `drive_test` | 全部硬件驱动自测（一个外设一页） | 待分配 |
| `imu` | 姿态解算 + 编码器里程计 + 航迹推算 | 待分配 |
| `keytest` | 按键引脚定位 | 待分配 |
| `menu` | 四级菜单 + 参数 Flash 掉电保存 | 待分配 |
| `patrol` | 循迹图像识别 | 待分配 |
| `screen` | 屏幕点亮验证 | 待分配 |
| `servo_test` | 舵机 PWM 输出 | 待分配 |

**只要每人只在自己负责的目录里改，就不会有冲突。** 请避免这几件事：

- ❌ 不要多人同时改根目录的 `README.md` / `CONTRIBUTING.md`（要改先说一声）
- ❌ **不要改任何 `libraries/` 下的文件** —— 那是逐飞官方库，8 个工程各有一份副本，改一处不会同步到别的工程，而且以后升级库会被覆盖
- ❌ 不要删别人目录里的文件

---

## 4. 日常流程

```bash
# 开工前先拉最新（队友可能刚推过）
git pull

# 改代码……（用 ADS 打开对应子目录的工程，Build + Debug 验证）

# 只提交你改的东西
git status                      # 看清楚改了哪些
git add camera/user/xxx.c       # 或 git add -A 只在你只碰了自己目录时用
git commit -m "camera: 加上二值化阈值按键调节"
git push
```

### commit 说明怎么写

一句话说清「改了哪个工程 + 改了什么」：

```
camera: 曝光调节改成按键长按连发
menu: 修复参数保存后标题不刷新
imu: 编码器每圈脉冲数改成 1024
```

不要写 `update`、`修改`、`111` 这种看不出内容的。

---

## 5. 关于 `Debug/` 目录

`Debug/` 是 ADS 编译产生的中间文件（`.o` / `.elf` / `.hex`），每个工程 60~70 MB，**已被 `.gitignore` 排除，不会上传**。

所以：

- 队友 clone 下来**没有** `Debug/` 目录是正常的，在 ADS 里 `Project → Build Project` 一次就有了
- `.launch` 调试配置也没上传，第一次点 `Debug` 时 ADS 会自己生成

---

## 6. 出现冲突怎么办

如果 `git push` 被拒绝，说 `rejected` 或 `non-fast-forward`，说明远端有你本地没有的提交：

```bash
git pull --rebase     # 把你的改动排到队友的提交后面
# 如果有冲突，打开冲突文件，保留两边都需要的内容，删掉 <<<<<<< ======= >>>>>>> 标记
git add <冲突文件>
git rebase --continue
git push
```

**不要**用 `git push -f` 强推 —— 会把队友的提交冲掉。真的搞乱了在群里说一声，别自己硬来。

改坏了想撤销单个文件：

```bash
git checkout -- 路径/文件名        # 丢弃这个文件未提交的改动
git revert <commit号>              # 撤销某个已提交的改动（安全，会生成新的反向提交）
```

---

## 7. 提 Issue 还是直接改？

- **改代码** → 直接改直接推（你是协作者，有写权限）
- **发现别人的模块有 bug / 想提需求** → 开个 Issue，@ 对应负责人，别自己去改别人的目录
- **要动公共部分**（根目录文档、`.gitignore`、整个工程目录结构）→ 先在群里说一声

---

## 8. 提交前自检清单

- [ ] 在 ADS 里 **Debug 配置**编译通过（0 error）
- [ ] 烧到板子上实际跑过，现象符合预期
- [ ] `git status` 里没有 `Debug/`、`*.elf`、`*.launch` 之类的编译产物被误加进去
- [ ] 中文注释在你的编辑器里显示正常（说明编码没被改坏）
- [ ] 没有动 `libraries/`

---

有问题直接在群里问，或者开 Issue。
