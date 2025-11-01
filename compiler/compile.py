"""
This is a program madfe to compile VR projects into C++ and generate the necessary OpenXR/Android Studio project
"""

import glob
import pathlib
from shutil import copy

try:
    XR_PROJECTS_ROOT = pathlib.Path("../XrSamples/").resolve()
    TEMPLATE_ROOT = XR_PROJECTS_ROOT / "VrEngine"
    TEMPLATE_EXCLUDES = [
        TEMPLATE_ROOT / "Project/Android/.cxx/",
        TEMPLATE_ROOT / "Project/Android/build/",
        TEMPLATE_ROOT / "Project/Android/.gradle/",
    ]
except Exception:
    exit(
        "Could not resolve template root path. Your installation is most like corrupted, or you have screwed with the file structure."
    )

PROJECTS_DIR = pathlib.Path("./projects/").resolve()


class Indicators:
    INCLUDES_ENTRY = "INCLUDES_ENTRY"
    INCLUDES_EXIT = "INCLUDES_EXIT"
    VAR_SPACE_ENTRY = "VAR_SPACE_ENTRY"
    VAR_SPACE_EXIT = "VAR_SPACE_EXIT"
    CLASS_ENTRY = "CLASS_ENTRY"
    CLASS_REGEX = f">@delimiter="
    CLASS_EXIT = "CLASS_EXIT"
    ENTRY_POINT_INIT = "ENTRY_POINT_INIT"
    ENTRY_POINT_EXIT = "ENTRY_POINT_EXIT"
    PUBLIC_ENTRY = "PUBLIC_ENTRY"
    PUBLIC_EXIT = "PUBLIC_EXIT"
    CLASS_INIT_ENTRY = "CLASS_INIT_ENTRY"
    CLASS_INIT_EXIT = "CLASS_INIT_EXIT"
    APP_EXTENSIONS_ENTRY = "APP_EXTENSIONS_ENTRY"
    APP_EXTENSIONS_MOD_ENTRY = "APP_EXTENSIONS_MOD_ENTRY"
    APP_EXTENSIONS_MOD_EXIT = "APP_EXTENSIONS_MOD_EXIT"
    APP_EXTENSIONS_EXIT = "APP_EXTENSIONS_EXIT"
    APP_INIT_ENTRY = "APP_INIT_ENTRY"
    APP_INIT_MOD_ENTRY = "APP_INIT_MOD_ENTRY"
    APP_INIT_MOD_EXIT = "APP_INIT_MOD_EXIT"
    APP_INIT_EXIT = "APP_INIT_EXIT"
    APP_SHUTDOWN_ENTRY = "APP_SHUTDOWN_ENTRY"
    APP_SHUTDOWN_MOD_ENTRY = "APP_SHUTDOWN_MOD_ENTRY"
    APP_SHUTDOWN_MOD_EXIT = "APP_SHUTDOWN_MOD_EXIT"
    APP_SHUTDOWN_EXIT = "APP_SHUTDOWN_EXIT"
    SESSION_INIT_ENTRY = "SESSION_INIT_ENTRY"
    SESSION_INIT_MOD_ENTRY = "SESSION_INIT_MOD_ENTRY"
    SESSION_INIT_MOD_EXIT = "SESSION_INIT_MOD_EXIT"
    SESSION_INIT_EXIT = "SESSION_INIT_EXIT"
    SESSION_END_ENTRY = "SESSION_END_ENTRY"
    SESSION_END_MOD_ENTRY = "SESSION_END_MOD_ENTRY"
    SESSION_END_MOD_EXIT = "SESSION_END_MOD_EXIT"
    SESSION_END_EXIT = "SESSION_END_EXIT"
    UPDATE_ENTRY = "UPDATE_ENTRY"
    UPDATE_MOD_ENTRY = "UPDATE_MOD_ENTRY"
    UPDATE_MOD_EXIT = "UPDATE_MOD_EXIT"
    UPDATE_EXIT = "UPDATE_EXIT"
    RENDER_ENTRY = "RENDER_ENTRY"
    RENDER_MOD_ENTRY = "RENDER_MOD_ENTRY"
    RENDER_MOD_EXIT = "RENDER_MOD_EXIT"
    RENDER_EXIT = "RENDER_EXIT"

    def all_indicators(return_type: type) -> list[str] | str | dict[str, str]:
        if return_type == str:
            return "|".join(
                [
                    value
                    for name, value in Indicators.__dict__.items()
                    if not name.startswith("__") and not callable(value)
                ]
            )
        elif return_type == list:
            return [
                value
                for name, value in Indicators.__dict__.items()
                if not name.startswith("__") and not callable(value)
            ]
        elif return_type == dict:
            return {
                name: value
                for name, value in Indicators.__dict__.items()
                if not name.startswith("__") and not callable(value)
            }


def parse_template_project(context: "Compiler"):
    NEW_PROJECT_DIR = XR_PROJECTS_ROOT / context.project.name
    template_files = glob.glob(str(TEMPLATE_ROOT), recursive=True)
    for file_path in template_files:
        file_path_obj = pathlib.Path(file_path)
        if any(
            str(file_path_obj).startswith(str(exclude)) for exclude in TEMPLATE_EXCLUDES
        ):
            continue
        relative_path = file_path_obj.relative_to(TEMPLATE_ROOT)
        new_file_path = NEW_PROJECT_DIR / relative_path
        new_file_path.parent.mkdir(parents=True, exist_ok=True)
        copy(file_path_obj, new_file_path)


class Parser:
    def __init__(self):
        self.indicators = Indicators.all_indicators()


class Project:
    def __init__(self, name):
        self.name = name


class Compiler:
    def __init__(self, project: Project):
        self.project: Project = project
        self.parser = Parser()

    def compile(self):
        parse_template_project(self)
        pass


if __name__ == "__main__":
    sample_project = Project("Test")
    compiler = Compiler(sample_project)
    compiler.compile()
