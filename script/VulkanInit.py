import os
import subprocess
import sys
from pathlib import Path

import Utils

from io import BytesIO
from urllib.request import urlopen
from zipfile import ZipFile

import colorama
from colorama import Fore
from colorama import Back
from colorama import Style

colorama.init()


class VulkanConfiguration:
    REQUIRED_VULKAN_VERSION = '1.3.'  # Any 1.3 version is fine
    INSTALL_VULKAN_VERSION = '1.3.216.0'  # Specifically install this one if no 1.3 version is present
    VULKAN_Windows_SDK_INSTALLER_URL = f'https://sdk.lunarg.com/sdk/download/{INSTALL_VULKAN_VERSION}/windows/VulkanSDK-{INSTALL_VULKAN_VERSION}-Installer.exe'
    VULKAN_SDK_LOCAL_PATH = 'Kans3D/vendor/VulkanSDK'
    VULKAN_SDK_PACKAGE_PATH = f'{VULKAN_SDK_LOCAL_PATH}/VulkanSDK.exe'

    @classmethod
    def Validate(cls):
        if (not cls.CheckVulkanSDK()):
            print(f"{Style.BRIGHT}{Back.RED}Can't Install VulkanSDK{Style.RESET_ALL}")
        if (not cls.CheckVulkanLibs()):
            print(f"{Style.BRIGHT}{Back.RED}Can't find the Vulkan SDK debug Libs{Style.RESET_ALL}")
            print(f"{Style.BRIGHT}{Back.YELLOW}Please follow the build guid in Readme file{Style.RESET_ALL}")

    @classmethod
    def CheckVulkanSDK(cls):
        VULKAN_SDK = os.environ.get('VULKAN_SDK')
        if (VULKAN_SDK is None):
            print(f"{Style.BRIGHT}{Back.YELLOW}Can't Find VulkanSDK,try install VulkanSDK{Style.RESET_ALL}")
            cls.__InstallVulkanSDK()
            return  False
        else:
            print(f"{Style.BRIGHT}Located Vulkan SDK at {VULKAN_SDK}{Style.RESET_ALL}")
        if (cls.REQUIRED_VULKAN_VERSION not in VULKAN_SDK):
            print(f"{Style.BRIGHT}{Back.YELLOW} Kans3D require VulkanSDK  version {cls.REQUIRED_VULKAN_VERSION} or higher{Style.RESET_ALL}")
            cls.__InstallVulkanSDK()
            return False
        return True

    @classmethod
    def CheckVulkanLibs(cls):
        VULKAN_SDK = os.environ.get("VULKAN_SDK")
        requestLibs = []
        requestLibs.append(Path(f"{VULKAN_SDK}/Lib/shaderc_sharedd.lib"))
        requestLibs.append(Path(f"{VULKAN_SDK}/Lib/spirv-cross-glsld.lib"))
        requestLibs.append(Path(f"{VULKAN_SDK}/spirv-cross-hlsld.lib"))
        requestLibs.append(Path(f"{VULKAN_SDK}/shaderc_utild.lib"))
        miss = False
        for path in requestLibs:
            if(not path.exists()):
                print(f"{Style.BRIGHT}{Back.Yellow}  {path} miss{Style.RESET_ALL}")
                miss = True
        return not miss

    @classmethod
    def __InstallVulkanSDK(cls):
        print("Would you like to install the Vulkan SDK?")
        install = Utils.YesOrNo()
        if (install):
            print('Downloading {} to {}'.format(cls.VULKAN_Windows_SDK_INSTALLER_URL, cls.VULKAN_SDK_PACKAGE_PATH))
            if(not Path(cls.VULKAN_SDK_LOCAL_PATH).exists()):
                Path(cls.VULKAN_SDK_LOCAL_PATH).mkdir()

            Utils.DownloadFile(cls.VULKAN_Windows_SDK_INSTALLER_URL, cls.VULKAN_SDK_PACKAGE_PATH)
            print("Running Vulkan SDK installer...")
            print(f"{Style.BRIGHT}{Back.YELLOW}Make sure to install shader debug libs ")
            os.startfile(os.path.abspath(cls.VULKAN_SDK_PACKAGE_PATH))
            print(f"{Style.BRIGHT}{Back.RED}Rerun the Script after VulkanSDK installation{Style.RESET_ALL}")
            quit()

if __name__ == "__main__":
    VulkanConfiguration.Validate()
