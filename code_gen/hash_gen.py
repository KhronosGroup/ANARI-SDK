# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import sys
import os
import json

def gen_hash_function_table_recursive(table, i, current, keywords, values):
   offset = len(table)
   crange = [ord(x[i]) for x in current]
   low = min(crange)
   high = max(crange)+1
   table.extend([0]*(high-low))
   for c in range(low, high):
      s = [x for x in current if i<len(x) and ord(x[i])==c]
      if s:
         if len(s) == 1 and c == 0:
            idx = keywords.index(s[0][0:-1])
            table[offset+c-low] = values[idx]+(0x80000000)
         else:
            table[offset+c-low] = gen_hash_function_table_recursive(table, i+1, s, keywords, values)

   return offset + (low<<16) + (high<<24)



def gen_hash_function_table(keywords, values):
   table = []
   root_keywords = [x+'\0' for x in keywords]
   root = gen_hash_function_table_recursive(table, 0, root_keywords, keywords, values)
   return (root, table)

def gen_hash_function(name, keywords, values=None, indent=""):
   if values==None:
      values = range(0, len(keywords))

   root, table = gen_hash_function_table(keywords, values)

   code  = ""
   code += "int %s(const char *str) {\n"%name
   code += indent + "   static const uint32_t table[] = {"
   code += ",".join(["0x%xu"%x for x in table])
   code += "};\n"
   code += indent + "   uint32_t cur = 0x%xu;\n"%root
   code += indent + "   for(int i = 0;cur!=0;++i) {\n"
   code += indent + "      uint32_t idx = cur&0xFFFFu;\n"
   code += indent + "      uint32_t low = (cur>>16u)&0xFFu;\n"
   code += indent + "      uint32_t high = (cur>>24u)&0xFFu;\n"
   code += indent + "      uint32_t c = str[i];\n"
   code += indent + "      if(c>=low && c<high) {\n"
   code += indent + "         cur = table[idx+c-low];\n"
   code += indent + "      } else {\n"
   code += indent + "         break;\n"
   code += indent + "      }\n"
   code += indent + "      if(cur&0x80000000u) {\n"
   code += indent + "         return cur&0xFFFFu;\n"
   code += indent + "      }\n"
   code += indent + "      if(str[i]==0) {\n"
   code += indent + "         break;\n"
   code += indent + "      }\n"
   code += indent + "   }\n"
   code += indent + "   return -1;\n"
   code += indent + "}\n"
   return code

def array_of_strings(name, keywords, values=None):
   code  = ""
   code += "const char *%s_strings[] = {"%name
   code += ",".join(["\"%s\""%x for x in keywords])
   code += "};\n"
   return code

def define_enums(name, keywords, values=None):
   if values==None:
      values = range(0, len(keywords))
   code  = ""
   code += "\n".join(["#define %s_%s %d"%(name.upper(), x.upper(), values[keywords.index(x)]) for x in keywords])
   code += "\n"
   return code
