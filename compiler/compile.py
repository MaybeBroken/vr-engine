"""
This is a program made to compile VR projects into C++ and generate the necessary OpenXR/Android Studio project
"""

import glob
import os
import pathlib
import shutil

try:
    XR_PROJECTS_ROOT = pathlib.Path("../XrSamples/").resolve()
    TEMPLATE_ROOT = XR_PROJECTS_ROOT / "VrEngine"
    TEMPLATE_EXCLUDES = [
        "Projects/Android/.cxx/",
        "Projects/Android/build/",
        "Projects/Android/.gradle/",
    ]
except Exception:
    exit(
        "Could not resolve template root path. Your installation is most like corrupted, or you have screwed with the file structure."
    )

PROJECTS_DIR = pathlib.Path("./projects/").resolve()

C_FILES = "Src"
ASSETS_DIR = "assets"


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

    def all_indicators(return_type: type = None) -> list[str] | str | dict[str, str]:
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
        elif return_type is None:
            return Indicators


def parse_template_project(context: "Compiler"):
    NEW_PROJECT_DIR = XR_PROJECTS_ROOT / context.project.name
    template_files = list(TEMPLATE_ROOT.rglob("*"))
    for file_path_obj in template_files:
        if any(
            (
                hasattr(pathlib.Path, "is_relative_to")
                and file_path_obj.is_relative_to(exclude)
            )
            or str(file_path_obj).startswith(str(exclude))
            for exclude in TEMPLATE_EXCLUDES
        ):
            continue
        if file_path_obj.is_dir():
            continue
        relative_path = file_path_obj.relative_to(TEMPLATE_ROOT)
        new_file_path = NEW_PROJECT_DIR / relative_path
        new_file_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(file_path_obj, new_file_path)
    for exclude in TEMPLATE_EXCLUDES:
        excluded_path = NEW_PROJECT_DIR / exclude
        if excluded_path.exists():
            if excluded_path.is_dir():

                shutil.rmtree(excluded_path)
            else:
                os.remove(excluded_path)
    print(f"Project {context.project.name} created at {NEW_PROJECT_DIR}")
    return NEW_PROJECT_DIR


class Parser:
    def __init__(self):
        self.indicators = Indicators.all_indicators()


class Project:
    def __init__(self, name):
        path = PROJECTS_DIR / name
        if not path.exists():
            raise FileNotFoundError(f"Project {name} does not exist in {PROJECTS_DIR}")
        self.name: str = name
        self.path: pathlib.Path = path
        self.assets_dir: pathlib.Path = self.path / ASSETS_DIR
        self.src_dir: pathlib.Path = self.path / C_FILES
        self.source_files: list[pathlib.Path] = []
        self.asset_files: list[pathlib.Path] = []
        print(
            f"Building project {self.name} located at {self.path}, scanning source files in {self.src_dir} and assets in {self.assets_dir}"
        )
        self.build_project()

    def resolve_src_files(self) -> list[pathlib.Path]:
        csource_files = glob.glob(str(self.src_dir) + "/**/*.csource", recursive=True)
        return [pathlib.Path(f) for f in csource_files]

    def resolve_asset_files(self) -> list[pathlib.Path]:
        asset_files = glob.glob(str(self.assets_dir) + "/**/*", recursive=True)
        return [pathlib.Path(f) for f in asset_files if os.path.isfile(f)]

    def build_project(self):
        self.source_files = self.resolve_src_files()
        self.asset_files = self.resolve_asset_files()
        main_file = self.src_dir / (self.name + ".csource")
        if main_file not in self.source_files:
            raise FileNotFoundError(
                f"Main source file {main_file} not found in project {self.name}"
            )
        # If we reach this point, the project is valid
        print(
            f"Project {self.name} is valid. Loading {len(self.source_files)} source files and {len(self.asset_files)} asset files."
        )


class Compiler:
    def __init__(self, project: Project):
        self.project: Project = project
        self.parser = Parser()

    def compile(self):
        parse_template_project(self)
        self.assemble_source()
        pass

    def assemble_source(self):
        self.project


if __name__ == "__main__":
    sample_project = Project("test")
    compiler = Compiler(sample_project)
    compiler.compile()
