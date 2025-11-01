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


class Parser:
    def __init__(self):
        self.indicators = Indicators.all_indicators()
