#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sqlite3
import shutil
import platform

# === 常量 ===
SEARCH_KEYWORD = "augment"  # 要在数据库键中搜索和清理的关键字

def get_vscode_db_path() -> str | None:
    """
    根据操作系统确定 VSCode 数据库的路径。
    返回路径字符串，如果操作系统不受支持或无法确定路径，则返回 None。
    """
    system = platform.system()
    try:
        if system == "Darwin":  # macOS
            return os.path.expanduser("~/Library/Application Support/Code/User/globalStorage/state.vscdb")
        elif system == "Windows":
            appdata = os.getenv('APPDATA')
            if appdata:
                return os.path.join(appdata, "Code", "User", "globalStorage", "state.vscdb")
            else:
                print("错误: 在 Windows 系统中未找到 APPDATA 环境变量。")
                return None
        elif system == "Linux":
            # VSCode 官方构建的常见路径
            return os.path.expanduser("~/.config/Code/User/globalStorage/state.vscdb")
        else:
            print(f"错误: 不支持的操作系统: {system}。无法确定 VSCode 数据库路径。")
            return None
    except Exception as e:
        print(f"获取数据库路径时发生意外错误: {e}")
        return None

def clean_vscode_db():
    """
    清理 VSCode 数据库中 'key' 包含 SEARCH_KEYWORD 的条目。
    在进行任何修改之前会备份数据库。
    """
    db_path = get_vscode_db_path()

    if not db_path:
        print("操作中止: 无法为当前操作系统确定 VSCode 数据库的路径。")
        print("请确认 VSCode 已安装，或在脚本中手动指定路径。")
        return

    if not os.path.exists(db_path):
        print(f"操作中止: 找不到数据库文件: {db_path}")
        print("请确认 VSCode 已安装或数据库文件位置正确。")
        return

    # 保持原始备份文件名格式
    backup_path = f"{db_path}.{platform.system().lower()}.backup"

    try:
        print(f"正在备份数据库 {db_path} 到 {backup_path}...")
        shutil.copy2(db_path, backup_path)
        print(f"数据库已成功备份到: {backup_path}")
    except IOError as e:
        print(f"数据库备份失败: {e}")
        print("操作已中止，未对数据库进行任何更改。")
        return  # 如果备份失败，则不继续

    sql_like_pattern = f'%{SEARCH_KEYWORD}%'

    try:
        # 使用 'with' 语句确保数据库连接正确管理（自动提交/回滚和关闭）
        with sqlite3.connect(db_path) as conn:
            cursor = conn.cursor()
            print(f"已连接到数据库: {db_path}")

            # 统计初始匹配的记录数
            cursor.execute("SELECT COUNT(*) FROM ItemTable WHERE key LIKE ?", (sql_like_pattern,))
            count = cursor.fetchone()[0]
            print(f"在 ItemTable 表中找到 {count} 条 'key' 包含 '{SEARCH_KEYWORD}' 的记录。")

            if count > 0:
                print(f"准备删除 {count} 条记录...")
                cursor.execute("DELETE FROM ItemTable WHERE key LIKE ?", (sql_like_pattern,))
                conn.commit()  # DML 操作需要显式提交

                rows_deleted = cursor.rowcount  # 受影响的行数
                print(f"成功删除了 {rows_deleted} 条记录。")

                # 验证删除
                cursor.execute("SELECT COUNT(*) FROM ItemTable WHERE key LIKE ?", (sql_like_pattern,))
                remaining_count = cursor.fetchone()[0]
                print(f"删除操作后，ItemTable 表中剩余 {remaining_count} 条匹配记录。")

                if remaining_count == 0:
                    print("所有匹配的记录已成功删除。")
                else:
                    # 如果 rows_deleted 与 count 匹配且提交成功，则不应发生此情况
                    print(f"警告: 仍有 {remaining_count} 条匹配记录。请检查数据库。")
            else:
                print("未找到需要删除的记录。")

        print("数据库操作完成。连接已关闭。")

    except sqlite3.Error as e:
        print(f"数据库操作发生错误: {e}")
        print("如果尝试了更改，这些更改可能未保存或部分保存。")
        print(f"备份文件位于: {backup_path} (如果备份成功)。")
    except Exception as e:
        print(f"发生未知错误: {e}")
        print(f"备份文件位于: {backup_path} (如果备份成功)。")

if __name__ == "__main__":
    clean_vscode_db()
