import os
import subprocess
import pathlib

if __name__ == '__main__':
    glslang_cmd = "glslangValidator"

    shader_list = [
        "gbuffer.vert", "gbuffer.frag",
        "shading.vert", "shading.frag",
        "depth_only.vert"
    ]

    for shader in shader_list:
        subprocess.run([glslang_cmd, "-V", shader, "-o -g", "{}.spv".format(shader)])

