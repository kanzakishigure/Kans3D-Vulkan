import os
import subprocess
import CheckRequest

CheckRequest.ValidatePackages()

from VulkanInit import VulkanConfiguration as VulkanConfig
import Utils

import colorama
from colorama import Fore
from colorama import Back
from colorama import Style

# change to root
os.chdir('../')
colorama.init()
if(not VulkanConfig.Validate()):
    exit()
# update submodule by git
print(f"{Style.BRIGHT}{Back.GREEN}Generating Visual Studio 2022 solution.{Style.RESET_ALL}")
subprocess.call(["vendor/bin/premake/premake5.exe", "vs2022"])