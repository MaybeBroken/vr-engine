"""
This is a program made to compile VR projects into C++ and generate the necessary OpenXR/Android Studio project
"""

import glob
import os
import pathlib
import shutil

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
try:
    XR_PROJECTS_ROOT = (SCRIPT_DIR.parent / "XrSamples").resolve()
    TEMPLATE_ROOT = SCRIPT_DIR / "src" / "engine" / "VrEngine"
    TEMPLATE_EXCLUDES = [
        "Projects/Android/.cxx/",
        "Projects/Android/build/",
        "Projects/Android/.gradle/",
    ]
except Exception:
    exit(
        "Could not resolve template root path. Your installation is most like corrupted, or you have screwed with the file structure."
    )

PROJECTS_DIR = (SCRIPT_DIR / "projects").resolve()

C_FILES = "Src"
ASSETS_DIR = "assets"


def str_format(s: str) -> str:
    """Formats code to remove comments and unnecessary whitespace."""
    lines = s.splitlines()
    formatted_lines = []
    for line in lines:
        stripped_line = line.strip()
        if (
            stripped_line.startswith("//")
            or stripped_line == ""
            or stripped_line.startswith("/*")
            or stripped_line.endswith("*/")
            or stripped_line == "#OVERRIDE"
        ):
            continue
        formatted_lines.append(line)
    return "\n".join(formatted_lines)


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

    headers = {
        "includes": {"entry": INCLUDES_ENTRY, "exit": INCLUDES_EXIT},
        "var_space": {"entry": VAR_SPACE_ENTRY, "exit": VAR_SPACE_EXIT},
        "class": {"entry": CLASS_ENTRY, "exit": CLASS_EXIT},
        "entry_point_init": {"entry": ENTRY_POINT_INIT, "exit": ENTRY_POINT_EXIT},
        "public": {"entry": PUBLIC_ENTRY, "exit": PUBLIC_EXIT},
        "class_init": {"entry": CLASS_INIT_ENTRY, "exit": CLASS_INIT_EXIT},
        "app_extensions": {"entry": APP_EXTENSIONS_ENTRY, "exit": APP_EXTENSIONS_EXIT},
        "app_init": {"entry": APP_INIT_ENTRY, "exit": APP_INIT_EXIT},
        "app_shutdown": {"entry": APP_SHUTDOWN_ENTRY, "exit": APP_SHUTDOWN_EXIT},
        "session_init": {"entry": SESSION_INIT_ENTRY, "exit": SESSION_INIT_EXIT},
        "session_end": {"entry": SESSION_END_ENTRY, "exit": SESSION_END_EXIT},
        "update": {"entry": UPDATE_ENTRY, "exit": UPDATE_EXIT},
        "render": {"entry": RENDER_ENTRY, "exit": RENDER_EXIT},
    }

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
    if NEW_PROJECT_DIR.exists():
        shutil.rmtree(NEW_PROJECT_DIR)
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

    def resolve_src_files(self) -> list[pathlib.Path]:
        csource_files = glob.glob(str(self.src_dir) + "/**/*", recursive=True)
        return [pathlib.Path(f) for f in csource_files]

    def resolve_asset_files(self) -> list[pathlib.Path]:
        asset_files = glob.glob(str(self.assets_dir) + "/**/*", recursive=True)
        return [
            pathlib.Path(f)
            for f in asset_files
            if os.path.isfile(f) and not ("main.cpp" in f)
        ]

    def build_project(self):
        self.source_files = self.resolve_src_files()
        self.asset_files = self.resolve_asset_files()
        main_file = self.src_dir / "main.cpp"
        if main_file not in self.source_files:
            raise FileNotFoundError(
                f"Main source file {main_file} not found in project {self.name}"
            )
        # If we reach this point, the project is valid
        print(
            f"Project {self.name} is valid. Loading {len(self.source_files)} source files and {len(self.asset_files)} asset files."
        )
        with open(main_file, "r", encoding="utf-8-sig", errors="ignore") as f:
            main_content = f.read()

        return self.parse(main_content)

    def parse(self, main_content: str):
        modifications = []
        # Always queue includes and assets first (order independent of marker stacking)
        for src_file in self.source_files:
            modifications.append({"action": "include", "file": src_file})
        for asset_file in self.asset_files:
            modifications.append(
                {
                    "action": "asset",
                    "file": asset_file,
                    "path": str(asset_file.relative_to(self.assets_dir)),
                }
            )

        # Sequential scan to support multiple overrides and markers stacked by appearance order
        lines = main_content.splitlines()
        i = 0
        known_markers = list(Indicators.headers.keys())
        while i < len(lines):
            line = lines[i]
            # Full-file override (legacy): if file starts with #OVERRIDE and no #end, treat whole file as override once
            if (
                i == 0
                and line.startswith("#OVERRIDE")
                and not any(l.startswith("#end") for l in lines[1:])
            ):
                print("Override detected (full-file).")
                modifications.append(
                    {"action": "override_root", "content": main_content}
                )
                break

            if line.startswith("#OVERRIDE"):
                # Block override up to #end
                start_index = i + 1
                end_index = None
                for j in range(start_index, len(lines)):
                    if lines[j].startswith("#end"):
                        end_index = j
                        break
                if end_index is None:
                    # No terminator, use rest of file
                    end_index = len(lines)
                content = "\n".join(lines[start_index:end_index])
                print("Override detected (block), stacking.")
                modifications.append({"action": "override_root", "content": content})
                i = end_index + 1
                continue

            # Check for any known marker at this line
            matched_marker = None
            matched_mode = None
            if line.startswith("#"):
                for marker in known_markers:
                    prefix = f"#{marker}"
                    if line.startswith(prefix):
                        matched_marker = marker
                        if ">" in line:
                            matched_mode = line.split(">", 1)[1].strip() or None
                        break

            if matched_marker is not None:
                start_index = i + 1
                end_index = None
                for j in range(start_index, len(lines)):
                    if lines[j].startswith("#end"):
                        end_index = j
                        break
                if end_index is None:
                    end_index = len(lines)
                content = "\n".join(lines[start_index:end_index])
                print(
                    f"Found {matched_marker} marker, stacking modification (mode={matched_mode or 'a'})."
                )
                modifications.append(
                    {
                        "action": "modification_marker",
                        "marker": matched_marker,
                        "mode": matched_mode,
                        "content": content,
                    }
                )
                i = end_index + 1
                continue

            i += 1

        print(f"Parsed project {self.name} with {len(modifications)} operations.")
        return modifications


class Compiler:
    def __init__(self, project: Project):
        self.project: Project = project
        self.parser = Parser()
        self.root_file = TEMPLATE_ROOT / C_FILES / "main.cpp"

    def compile(self):
        self.project_dir = parse_template_project(self)
        self.assemble_source()
        pass

    def assemble_source(self):
        self.compiled_project = self.project.build_project()
        with open(self.root_file, "r", encoding="utf-8-sig", errors="ignore") as f:
            root_content = f.read()

        for modification in self.compiled_project:
            if modification["action"] == "override_root":
                print("Applying root override (stacked).")
                root_content = modification["content"]
            elif modification["action"] == "include":
                print(f"Including source file {modification['file']}.")
                shutil.copy(
                    modification["file"],
                    self.project_dir / C_FILES / modification["file"].name,
                )
            elif modification["action"] == "asset":
                print(
                    f"Adding asset file {modification['file']} at {modification['path']}."
                )
                asset_dest = self.project_dir / ASSETS_DIR / modification["path"]
                asset_dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(modification["file"], asset_dest)
            elif modification["action"] == "modification_marker":
                marker = modification["marker"]
                entry_indicator = f"// {Indicators.headers[marker]['entry']}"
                exit_indicator = f"// {Indicators.headers[marker]['exit']}"
                print(
                    f"Applying modification for marker {marker}, mode {modification.get('mode') or 'a'}."
                )
                if entry_indicator in root_content and exit_indicator in root_content:
                    start_index = root_content.index(entry_indicator) + len(
                        entry_indicator
                    )
                    end_index = root_content.index(exit_indicator)
                    mode = (modification.get("mode") or "a").lower()
                    if mode == "w":
                        root_content = (
                            root_content[:start_index]
                            + "\n"
                            + modification["content"]
                            + "\n"
                            + root_content[end_index:]
                        )
                    elif mode == "a":
                        root_content = (
                            root_content[:end_index]
                            + "\n"
                            + modification["content"]
                            + "\n"
                            + root_content[end_index:]
                        )
                    elif mode == "p":
                        root_content = (
                            root_content[:start_index]
                            + "\n"
                            + modification["content"]
                            + "\n"
                            + root_content[start_index:]
                        )
        with open(
            self.project_dir / C_FILES / "main.cpp",
            "w",
        ) as f:
            f.write(str_format(root_content))

        with open(
            self.project_dir / "CMakeLists.txt",
            "r",
        ) as f:
            cmake_content = f.read()
        cmake_content = cmake_content.replace(
            "project(VrEngine)",
            f"project({self.project.name.replace('-', '_').replace(' ', '_')})",
        )
        with open(
            self.project_dir / "CMakeLists.txt",
            "w",
        ) as f:
            f.write(cmake_content)

        with open(
            self.project_dir / "Projects/Android/build.gradle",
            "r",
        ) as f:
            gradle_content = f.read()
        gradle_content = gradle_content.replace(
            'targets "VrEngine"',
            f"targets \"{self.project.name.replace('-', '_').replace(' ', '_')}\"",
        )
        with open(
            self.project_dir / "Projects/Android/build.gradle",
            "w",
        ) as f:
            f.write(gradle_content)

        with open(
            self.project_dir / "Projects/Android/AndroidManifest.xml",
            "r",
        ) as f:
            manifest_content = f.read()
        manifest_content = manifest_content.replace(
            'android:value="VrEngine"',
            f'android:value="{self.project.name.replace("-", "_").replace(" ", "_")}"',
        )
        manifest_content = manifest_content.replace(
            'package="com.maybebroken.vrengine"',
            f'package="com.maybebroken.vrengine.{self.project.name.replace("-", "_").replace(" ", "_")}"',
        )
        manifest_content = manifest_content.replace(
            'android:label="VrEngine"',
            f'android:label="{self.project.name}"',
        )
        manifest_content = manifest_content.replace(
            'android:name="com.maybebroken.vrengine.MainActivity"',
            f'android:name="com.maybebroken.vrengine.{self.project.name.replace("-", "_").replace(" ", "_")}.MainActivity"',
        )
        with open(
            self.project_dir / "Projects/Android/AndroidManifest.xml",
            "w",
        ) as f:
            f.write(manifest_content)
        with open(
            self.project_dir / "res/values/strings.xml",
            "r",
        ) as f:
            strings_content = f.read()
        strings_content = strings_content.replace(
            '<string name="app_name">VrEngine</string>',
            f'<string name="app_name">{self.project.name}</string>',
        )
        with open(
            self.project_dir / "res/values/strings.xml",
            "w",
        ) as f:
            f.write(strings_content)


def open_android_studio(project_path: pathlib.Path):
    import subprocess
    import sys

    android_studio_path = ""
    if sys.platform == "win32":
        android_studio_path = (
            "C:\\Program Files\\Android\\Android Studio\\bin\\studio64.exe"
        )
    elif sys.platform == "darwin":
        android_studio_path = "/Applications/Android Studio.app/Contents/MacOS/studio"
    elif sys.platform == "linux":
        android_studio_path = "/usr/local/android-studio/bin/studio.sh"

    if not os.path.exists(android_studio_path):
        print(
            f"Android Studio executable not found at {android_studio_path}. Please open the project manually."
        )
    else:
        subprocess.Popen(
            [
                android_studio_path,
                str(project_path) + "/Projects/Android/build.gradle",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        print("Android Studio launched with the generated project.")


if __name__ == "__main__":
    project = Project(input("Enter the name of the project to compile: "))
    compiler = Compiler(project)
    compiler.compile()

    open_in_android_studio = input(
        "Do you want to open the generated project in Android Studio? (y/n): "
    )
    if open_in_android_studio.lower() == "y":
        open_android_studio(compiler.project_dir)


def build_project(project_name: str, open_in_android_studio: bool = False):
    project = Project(project_name)
    compiler = Compiler(project)
    compiler.compile()

    if open_in_android_studio:
        open_android_studio(compiler.project_dir)
