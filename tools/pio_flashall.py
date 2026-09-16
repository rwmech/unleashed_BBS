# File:        tools/pio_flashall.py
# Description: PlatformIO extra script. Adds "pio run -t flashall", which
#              uploads the firmware and then the data/ filesystem image.
#              Plain "pio run -t upload" is unchanged and leaves LittleFS
#              (screens, system.cfg, calls.log) alone.
#              Warning: flashall rewrites the whole storage partition,
#              which erases calls.log (and user data once C2 lands).
# Listing:     COMPLETE FILE
# Libraries:   PlatformIO SCons environment

Import("env")  # noqa: F821  (injected by PlatformIO)

port = env.GetProjectOption("upload_port", "")
port_arg = f" --upload-port {port}" if port else ""

env.AddCustomTarget(  # noqa: F821
    name="flashall",
    dependencies=None,
    actions=[
        f'"$PYTHONEXE" -m platformio run -e $PIOENV -t upload{port_arg}',
        f'"$PYTHONEXE" -m platformio run -e $PIOENV -t uploadfs{port_arg}',
    ],
    title="Flash All",
    description="Upload firmware, then the data/ filesystem image",
)
