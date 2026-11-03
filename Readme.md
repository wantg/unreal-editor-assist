# Unreal Editor Assist

![preview](https://media.githubusercontent.com/media/wantg/unreal-engine-assist/main/UnrealEngineAssist.png)

可在终端调用 UE 原生 `BuildPlugin` 编译并打包插件，无需游戏项目：

- macOS：将 `Build.sh` 中的 `UE_HOME` 改为本机引擎路径，然后运行 `./Build.sh`。
- Windows：将 `BuildPlugin.bat` 中的 `EngineDirectory` 改为本机引擎路径，在插件根目录运行 `BuildPlugin.bat`。

构建产物位于插件目录下的 `Build/`，不会自动回写原插件的 `Binaries`。要使用新版本，安装 `Build/` 中的插件并重启 UE。`Build.sh` 自动识别脚本所在目录中唯一的 `.uplugin` 文件，可直接复制到其他插件根目录使用；未找到或找到多个时会报错退出。Windows 脚本仍指定 `Assist.uplugin`。

重新编译插件并重启编辑器后，在 **Assist → Flexible Layout** 启用自由工作区布局（实验功能）：

- 左侧是通用工具面板区，首次默认放 Content Browser 1，占窗口宽度的 20%。
- 右侧上方保留主窗口原有的关卡及资源编辑器标签区；下方是第二个通用工具面板区，首次默认放 Output Log，占右侧可停靠区域高度的 25%。
- 当前编辑器的原生状态栏位于整个主窗口最底部，与主菜单使用相同的编辑器选择逻辑；将多个资源编辑器分组并排停靠时，也会跟随最近使用的编辑器切换。

两个工具区使用 UE 原生停靠系统，可将允许跨区停靠的通用工具面板拖入、组合成标签或继续拆分，也可以在左右两个工具区之间移动。蓝图图表、资源专属设置等受编辑器限制的面板保留其原有归属，不适配到通用工具区。

工具面板标签栏默认显示。单面板标签组可由用户通过右键菜单中的 **Hide Tabs** 隐藏，再点击左上角的原生“显示标签”小三角恢复显示。插件不会在新建或拖入面板时自动隐藏标签栏；用户已保存的显示或隐藏选择会正常恢复。组合多个面板时，UE 按原生规则显示标签以供切换。移走或关闭工具区中的最后一个面板后，该区域自动折叠，编辑器使用腾出的空间。拖拽可被该区域接受的通用工具标签（`NomadTab`）时，空工具区才会临时展开为原生停靠目标；拖拽资产编辑器主标签、编辑器专属面板或资源文件时保持折叠；放入面板后保留，未放入则重新折叠。分隔比例仍会保留，原生侧边栏中尚有工具的区域也不会被误判为空。

UE 会分别保存主布局与两个工具区的布局，包括分隔线比例、标签组合和隐藏状态，供下次启动恢复。工具区使用 UE 原生延迟保存，拖拽期间不从外层保存回调同步收集临时预览区；拖拽结束后保存最终布局。再次点击 **Flexible Layout** 会恢复外层默认比例，并沿用已经保存的工具区内容。左侧 CB 使用小图标列表。

应用布局时保留主窗口的位置、尺寸及最大化状态。切换时复用原生主窗口并重建内部停靠内容，已打开的资源编辑器按 UE 当前设置恢复；内部界面及独立编辑器窗口仍可能短暂刷新。Play / Simulate 期间不能应用布局。

插件遵循用户的 **Asset Editor Open Location** 设置，不修改资源编辑器的打开位置。点击通用工具区时，主菜单保留最近使用的主窗口编辑器菜单；工具面板正常获得键盘焦点，保留其搜索、快捷键和右键操作。独立资源编辑器窗口保持 UE 原有行为。
主窗口菜单由当前编辑器统一提供，工具区和资产编辑器初始化时的空菜单更新不会清空它；有效菜单就绪后再切换。菜单及标题区高度在 Slate 绘制前同步，保持左侧工具区和编辑器区的菜单留白一致。点击 **Flexible Layout** 时，macOS 使用当前 Cocoa 客户区的位置和尺寸作为临时重建参数，跳过 UE 对已存在主窗口再次按全屏尺寸构造并最大化的步骤，避免引入原生绘制区域与 Metal 渲染视口的尺寸偏差。临时参数仅在重建调用期间生效，原有启动位置、尺寸和最大化设置随后恢复。重建后进行一次几何状态同步：渲染尺寸重新跟随窗口，通过 UE 原生窗口尺寸通知重新读取 Cocoa 视图的位置及尺寸，再重建点击检测缓存；后续移动、最大化/还原交给 UE 正常处理。

**Assist → Restore Default Layout** 直接复用系统 **Window → Load Layout → Default Editor Layout** 菜单的原始操作，加载及确认流程由 UE 处理。插件不创建或还原自定义布局备份；工具区使用 UE 原生布局保存接口。

布局与状态栏回归测试位于 `Assist.Layout` 自动化测试组；`Assist.Menus.GenericEditorStartupBinding` 检查实际启动后的资产编辑器菜单事件绑定，`Assist.Menus.ToolkitContextAndOwnership` 覆盖专用编辑器菜单注册、Reload 上下文及卸载清理。按工程约定不自动编译，UE 编译、自动化测试和界面验证另行执行。

界面验证应在 Windows、macOS 覆盖：

- 重启恢复工具位置、标签分组、隐藏状态和分隔线比例；反复应用布局不重复创建工具面板或嵌套区域。
- 将通用工具（包括启用 Nomad 的 `EUW_ConsoleCommands`）在左侧、底部、浮动窗口间移动，拖拽悬停超过 5 秒再落下；清空区域后确认空白区域及分隔线消失，再拖拽通用工具标签确认可接受的停靠目标重新出现，拖拽资产编辑器/Details 等受限面板时空区保持折叠；重启确认只恢复最终停靠位置、日志不出现重复主区域断言；确认编辑器专属面板无法拖入通用区。
- 新建及拖入工具面板时标签栏默认显示；单面板的原生 **Hide Tabs**、显示标签小三角及多面板切换正常，手动显示或隐藏后重启仍保持选择。
- 关卡/蓝图/纹理编辑器切换，并将多个编辑器分组并排停靠；确认主菜单、Reload 目标和底部状态栏随所用编辑器一致切换，点击工具区后保持归属；检查工具搜索输入及 Content Drawer / Output Log 抽屉。
- 在普通窗口及已最大化窗口中反复点击 **Flexible Layout**，确认窗口位置、尺寸和最大化状态保持，macOS 日志不持续出现 `Viewport Size Mismatch`。分别在刚应用布局（不重启）及重启恢复后，手动缩放、移动、双击标题栏最大化/还原、切换不同 DPI 显示器，确认按钮、主菜单及弹出菜单位置与点击位置一致；加载系统默认布局后主窗口尺寸、菜单和状态栏正常。

Editor Utility Widget 面板需要在其蓝图资产的 Settings 中启用 **Spawn As Nomad Tab**（`bSpawnAsNomadTab`），UE 才会将其注册为可跨区停靠的通用工具。保存后关闭原有面板，再从插件入口重新打开。

例如 Console Cmds 的入口通过 `UEditorUtilitySubsystem::SpawnAndRegisterTab` 打开 `/ConsoleCommands/EUW_ConsoleCommands`。该资产未启用 Nomad 时会使用普通的编辑器局部标签，无法拖入通用工具区；应启用上述选项，而非放宽容器对资源专属面板的限制。Assist 不自动修改其他插件的蓝图资产或注册方式。

Assist 菜单除了配置中的编辑器，还会在标准 `FAssetEditorToolkit` 编辑器创建界面前，按其实际菜单名称注册，包括专用编辑器和 UE 通用资产编辑器。配置中的 Behavior Tree、Environment Query、Niagara 和 NiagaraScriptEditor 使用原生 toolkit 名称。菜单系统早于 `GEditor` 初始化时，插件会在引擎初始化完成后绑定资产编辑器事件；插件中途加载时则立即绑定。DataAsset、PrimaryDataAsset 及自定义派生类型（例如 `InvItemDefinition`）无需逐一加入 `supportedEditors`；同时编辑多个资产时，也支持 UE 使用的通用菜单。打开相同类型的资产不会重复添加 Assist 菜单。

菜单注册日志使用 `LogAssist` 分类：`Asset editor menu hook registered.` 表示事件已绑定；首次打开未在配置中注册的编辑器时，`Registered asset editor menu: AssetEditor.<实际菜单名>.MainMenu` 表示该类型已完成注册。

**Assist → Reload** 直接使用打开该菜单的 toolkit 上下文，重载该编辑器涉及的资源包，并对同一包去重；不依赖窗口焦点或遍历其他已打开的编辑器。上下文缺失或编辑器已关闭时禁用操作；只有关卡编辑器菜单的 Reload 才以当前关卡为目标。Play / Simulate 期间不执行资源重载。
