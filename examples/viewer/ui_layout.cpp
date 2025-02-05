// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

static const char *g_defaultLayout =
R"layout(
[Window][MainDockSpace]
Pos=0,25
Size=1920,1174
Collapsed=0

[Window][Viewport]
Pos=550,25
Size=1370,1174
Collapsed=0
DockId=0x00000002,0

[Window][Lights Editor]
Pos=0,611
Size=548,588
Collapsed=0
DockId=0x00000004,0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Scene]
Pos=0,25
Size=548,584
Collapsed=0
DockId=0x00000003,0

[Docking][Data]
DockSpace     ID=0x782A6D6B Pos=0,25 Size=1920,1174 CentralNode=1
DockSpace     ID=0x80F5B4C5 Window=0x079D3A04 Pos=0,25 Size=1920,1174 Split=X Selected=0xC450F867
  DockNode    ID=0x00000001 Parent=0x80F5B4C5 SizeRef=548,1174 Split=Y Selected=0x9BF64AD9
    DockNode  ID=0x00000003 Parent=0x00000001 SizeRef=548,584 Selected=0xE601B12F
    DockNode  ID=0x00000004 Parent=0x00000001 SizeRef=548,588 Selected=0x9BF64AD9
  DockNode    ID=0x00000002 Parent=0x80F5B4C5 SizeRef=1370,1174 CentralNode=1 Selected=0xC450F867
)layout";

const char *getDefaultUILayout()
{
  return g_defaultLayout;
}
