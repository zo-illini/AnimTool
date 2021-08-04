# AnimTool

对动画序列进行批量选取，缩放，以及步态自动标识的插件工具。

将Plugin/AnimCurveTool文件夹放入其他项目的Plugin文件夹后，通过工具栏打开
![企业微信截图_162744162237](https://user-images.githubusercontent.com/22382526/127257711-6a94f716-2fac-44e6-aa0e-8f1ca28e0cd2.png)

# 动画选择

在内容浏览器里高亮想要的动画序列，然后点击“add from content browswer”加入分组内。点击“Reset Group”清空现有分组。
按钮下方显示了现有的分组。

![企业微信截图_16274416834465](https://user-images.githubusercontent.com/22382526/127257898-352b6864-040c-41c0-9444-0a849f7761b0.png)

# 播放速率缩放

在缩放工具下，可以通过前后缀进一步筛选想要处理的动画。

![企业微信截图_16274417018947](https://user-images.githubusercontent.com/22382526/127258059-72ba2fe0-59b4-4c8e-be1d-4e1171b9ab97.png)


输入数字并点击Apply，第一栏用于按数字比率更改动画的播放速度，第二栏则会单独计算每个动画的根运动速度，并分别更改播放速度，使根运动速度符合输入值。
输入栏下方显示了正接受处理的动画。

![企业微信截图_16274417168047](https://user-images.githubusercontent.com/22382526/127258176-107d5a2b-bcba-4da1-b90e-44f72f25d86a.png)

# 步态同步组

分别输入左脚与右脚骨骼的名称，点击“precalculate”，已在分组内的动画会进行预计算并加入同步组，显示在下方。可以通过clear按钮清空当前同步组。

![企业微信截图_16274417725652](https://user-images.githubusercontent.com/22382526/127258780-13a3d886-adb2-4f42-bb8b-0c9286d3f200.png)

选择当前同步组内的一个动画作为参考，并输入轨道名，点击下方的Sync按钮后，选定轨道上的动画通知与同步组标记会被复制到同步组内的所有动画上。新生成的动画与同步组标记，时间上遵从同步组内的同步逻辑。
简单举例来说，如果参照动画Walk_F上，在双脚分别触地时各有一个标记。点击Sync后，同步组内的所有移动动画，都应该获得在双脚触底的时刻的标记。

![企业微信截图_16274418044727](https://user-images.githubusercontent.com/22382526/127258940-d9dd0851-8f5d-49aa-a743-121d3b630491.png)
