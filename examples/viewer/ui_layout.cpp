// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

static const char *g_defaultLayout =
R"layout(
[Window][MainDockSpace]
Pos=0,25
Size=1920,1174
Collapsed=0

[Window][Viewport]
Pos=551,25
Size=1369,1174
Collapsed=0
DockId=0x00000002,0

[Window][Lights Editor]
Pos=0,310
Size=549,889
Collapsed=0
DockId=0x00000004,0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Scene]
Pos=0,25
Size=549,283
Collapsed=0
DockId=0x00000003,0

[Docking][Data]
DockSpace     ID=0x782A6D6B Window=0xDEDC5B90 Pos=0,25 Size=1920,1174 Split=X
  DockNode    ID=0x00000001 Parent=0x782A6D6B SizeRef=549,1174 Split=Y Selected=0x5098EBE6
    DockNode  ID=0x00000003 Parent=0x00000001 SizeRef=549,283 Selected=0xE192E354
    DockNode  ID=0x00000004 Parent=0x00000001 SizeRef=549,889 Selected=0x5098EBE6
  DockNode    ID=0x00000002 Parent=0x782A6D6B SizeRef=1369,1174 CentralNode=1 Selected=0x13926F0B
)layout";

const char *getDefaultUILayout()
{
  return g_defaultLayout;
}
