// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

static const char *g_defaultLayout = R"([Window][MainDockSpace]
Pos=0,25
Size=2471,1249
Collapsed=0

[Window][Viewport]
Pos=369,25
Size=2102,1249
Collapsed=0
DockId=0x00000002,0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Scene]
Pos=0,25
Size=367,1249
Collapsed=0
DockId=0x00000003,0

[Window][Performance Metrics]
Pos=369,25
Size=3471,409
Collapsed=0
DockId=0x00000001,0

[Docking][Data]
DockSpace     ID=0x782A6D6B Window=0xDEDC5B90 Pos=0,25 Size=2471,1249 Split=X Selected=0x13926F0B
  DockNode    ID=0x00000003 Parent=0x782A6D6B SizeRef=367,2045 Selected=0xE192E354
  DockNode    ID=0x00000004 Parent=0x782A6D6B SizeRef=3471,2045 Split=Y
    DockNode  ID=0x00000001 Parent=0x00000004 SizeRef=3840,409 Selected=0xB1391C1F
    DockNode  ID=0x00000002 Parent=0x00000004 SizeRef=3840,1634 CentralNode=1 Selected=0x13926F0B)";

const char *getDefaultUILayout()
{
  return g_defaultLayout;
}