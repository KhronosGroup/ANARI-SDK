// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

static const char *g_defaultLayout = R"(
[Window][MainDockSpace]
Pos=0,25
Size=1920,1054
Collapsed=0

[Window][Viewport]
Pos=368,25
Size=1552,1054
Collapsed=0
DockId=0x00000004,0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Scene]
Pos=0,25
Size=366,1054
Collapsed=0
DockId=0x00000003,0

[Window][Performance Metrics]
Pos=369,25
Size=3471,409
Collapsed=0
DockId=0x00000001,0

[Docking][Data]
DockSpace   ID=0x782A6D6B Pos=0,25 Size=2471,1249 Split=Y Selected=0x13926F0B
  DockNode  ID=0x00000001 Parent=0x782A6D6B SizeRef=3840,409 Selected=0xB1391C1F
  DockNode  ID=0x00000002 Parent=0x782A6D6B SizeRef=3840,1634 CentralNode=1 Selected=0x13926F0B
DockSpace   ID=0x80F5B4C5 Window=0x079D3A04 Pos=0,25 Size=1920,1054 Split=X
  DockNode  ID=0x00000003 Parent=0x80F5B4C5 SizeRef=366,1054 Selected=0xE601B12F
  DockNode  ID=0x00000004 Parent=0x80F5B4C5 SizeRef=1552,1054 CentralNode=1 Selected=0xC450F867
)";

const char *getDefaultUILayout()
{
  return g_defaultLayout;
}