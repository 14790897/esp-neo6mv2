# extra_script.py
import subprocess
import sys
from SCons.Script import AlwaysBuild, Default, DefaultEnvironment, Exit
print("====================================")
print("EXTRA SCRIPT IS RUNNING!")
print("====================================")

Import("env")

# env.Replace(MONITOR_DTR=0, MONITOR_RTS=0)
# # 设置串口监视器参数，确保上传后自动复位（适用于ESP8266/ESP32等）
# env['MONITOR_DTR'] = 0
# env['MONITOR_RTS'] = 0

# PlatformIO会读取platformio.ini中的monitor_dtr/monitor_rts参数，
# 但此处强制设置也可确保脚本生效。


def build_and_upload_fs(source, target, env):
    print("Building filesystem...")
    env.Execute(
        f'$PYTHONEXE -m platformio run --target buildfs --environment {env["PIOENV"]}'
    )
    print("Uploading filesystem...")
    env.Execute(
        f'$PYTHONEXE -m platformio run --target uploadfs --environment {env["PIOENV"]}'
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", build_and_upload_fs)
