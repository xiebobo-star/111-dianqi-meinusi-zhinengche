# -*- coding: utf-8 -*-
"""
TC264 菜单工程 —— 汉字点阵生成脚本

扫 user/ 下所有 .c/.h 里"双引号字符串"中的汉字，用 16px 宋体取模，生成 user/menu_font.c
点阵格式与逐飞库 ips200_show_chinese / tft180_show_chinese 一致：
    每个汉字 32 字节 = 16 行 x 2 字节，逐行取模；每行第 1 字节 = 左边 8 点、第 2 字节 = 右边 8 点，高位在左
用法：python tools/gen_font.py      （改完菜单文字以后重新跑一次，把新出现的汉字补进字库）
"""
import os
import re

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
USER_DIR = os.path.join(HERE, "..", "user")
OUT_C = os.path.join(USER_DIR, "menu_font.c")
FONT = ImageFont.truetype(r"C:\Windows\Fonts\simsun.ttc", 16)      # 宋体 16 像素

STR_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')

HEADER = """/*********************************************************************************************************************
* 文件名            menu_font.c
* 说明              16x16 汉字点阵表（自动生成，不要手改）
*                   生成方式：python tools/gen_font.py —— 扫 user/ 下所有 .c/.h 字符串里的汉字，用 16px 宋体取模
*                   格式与逐飞库 *_show_chinese 一致：32 字节 = 16 行 x 2 字节，逐行取模，高位在左
********************************************************************************************************************/
#include "menu_font.h"

const menu_cn16_struct menu_cn16_table[] =
{
"""

TAIL = """};

const uint16 menu_cn16_table_size = (uint16)(sizeof(menu_cn16_table) / sizeof(menu_cn16_table[0]));

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     按 GBK 编码查字模
// 参数说明     code_high / code_low    一个汉字的两个字节（GBK）
// 返回参数     32 字节点阵首地址；字库里没有这个字返回 NULL（显示时会跳过，不会乱）
//-------------------------------------------------------------------------------------------------------------------
const uint8 *menu_font_get (uint8 code_high, uint8 code_low)
{
    uint16 code = (uint16)(((uint16)code_high << 8) | code_low);
    uint16 i;

    for (i = 0; i < menu_cn16_table_size; i ++)
    {
        if (code == menu_cn16_table[i].code)
        {
            return menu_cn16_table[i].data;
        }
    }
    return NULL;
}
"""


def collect_chars():
    chars = set()
    for name in sorted(os.listdir(USER_DIR)):
        if not name.endswith((".c", ".h")):
            continue
        if name == "menu_font.c":
            continue
        text = open(os.path.join(USER_DIR, name), encoding="gbk", errors="replace").read()
        for m in STR_RE.finditer(text):
            for ch in m.group(1):
                if ord(ch) > 0x7F:
                    chars.add(ch)
    return sorted(chars)


def glyph(ch):
    """返回 32 字节：逐行取模，每行 2 字节（左 8 点 / 右 8 点），高位在左"""
    img = Image.new("1", (16, 16), 0)
    ImageDraw.Draw(img).text((0, 0), ch, font=FONT, fill=1)
    px = img.load()
    out = []
    for y in range(16):
        for bx in (0, 8):
            b = 0
            for i in range(8):
                if px[bx + i, y]:
                    b |= (0x80 >> i)
            out.append(b)
    return out


def show(ch, data):
    print("  %s:" % ch)
    for y in range(16):
        line = "".join("#" if (data[y * 2 + (i // 8)] & (0x80 >> (i % 8))) else "." for i in range(16))
        print("    " + line)


def main():
    chars = collect_chars()
    print("共找到 %d 个汉字：%s" % (len(chars), "".join(chars)))

    rows = []
    for ch in chars:
        code = ch.encode("gbk")
        rows.append(((code[0] << 8) | code[1], ch, glyph(ch)))

    for ch in "电机参数保存成功":
        for code, c, data in rows:
            if c == ch:
                show(ch, data)
                break

    with open(OUT_C, "w", encoding="gbk", newline="\r\n") as f:
        f.write(HEADER)
        for code, ch, data in rows:
            body = ", ".join("0x%02X" % b for b in data)
            f.write("    {0x%04X, {%s}},    // %s\n" % (code, body, ch))
        f.write(TAIL)

    print("已生成 %s（%d 个汉字）" % (OUT_C, len(rows)))


if __name__ == "__main__":
    main()
