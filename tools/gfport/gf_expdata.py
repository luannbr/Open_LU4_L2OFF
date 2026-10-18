"""Writes expdata.txt with the GF experience table.

usage: python gf_expdata.py <output expdata.txt>

GF has no expdata.txt: its L2Server.exe holds the table (int64 values from
file offset 0x675450 in the GF Classic L2Server.exe). Row N is the experience
needed to reach level N + 1, as in this server's expdata.txt. GF ends at level
85, so rows 85 to 121 all hold the experience of level 86: L2Server caps a
character at ExpByLevel[120] - 1 and never reaches level 86.

convert_skillenchant.py imports GF_EXP for the GF enchant costs.
"""
import sys

GF_EXP = [
    0, 68, 363, 1168, 2884, 6038, 11287, 19423, 31378, 48229,
    71202, 101677, 141193, 191454, 254330, 331867, 426288, 540000, 675596, 835862,
    1023784, 1242546, 1495543, 1786379, 2118876, 2497077, 2925250, 3407897, 3949754, 4555796,
    5231246, 5981576, 6812513, 7730044, 8740422, 9850166, 11066072, 12395215, 13844951, 15422929,
    17137087, 18995665, 21007203, 23180550, 25524868, 28049635, 30764654, 33680052, 36806289, 40154162,
    45525133, 51262490, 57383988, 63907911, 70853089, 80700831, 91162654, 102265881, 114038596, 126509653,
    146308200, 167244337, 189364894, 212717908, 237352644, 271975263, 308443198, 346827154, 387199547, 429634523,
    474207979, 532694979, 606322775, 696381369, 804225364, 931275828, 1151275834, 1511275834, 2099275834, 4200000000,
    6300000000, 8820000000, 11844000000, 15472800000, 19827360000, 25314105600,
]
MAX_LEVEL = 85
LAST_ROW = 121  # this server's table has rows 0..121 (ExpByLevel[122])

if __name__ == "__main__":
    rows = ["exp_table_begin"]
    for level in range(LAST_ROW + 1):
        exp = GF_EXP[min(level, MAX_LEVEL)]
        rows.append(f" level={level} exp={exp} training_rate=0")
    rows.append("exp_table_end")
    open(sys.argv[1], "w", encoding="utf-16", newline="").write("\r\n".join(rows) + "\r\n")
    print(f"GF table to level {MAX_LEVEL}; rows {MAX_LEVEL}-{LAST_ROW} hold the level-{MAX_LEVEL + 1} cap")
