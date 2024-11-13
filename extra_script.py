import os
import subprocess
from SCons.Script import Import

Import("env")

def before_build(source, target, env):
    js_project_path = os.path.join(env['PROJECT_DIR'], 'js_project')
    os.chdir(js_project_path)
    subprocess.check_call(["npm", "install"])
    subprocess.check_call(["npm", "run", "build"])
    
env.AddPreAction("buildprog", before_build)