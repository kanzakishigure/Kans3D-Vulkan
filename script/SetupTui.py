"""Interactive front end of Setup.py, built with Textual (vendored wheels in script/third_party).

Setup.py owns the dependency tables, checks and install commands; this file only presents them.
Installing runs in the normal terminal (the app is suspended) so sudo/winget prompts work;
configure and build stream their output into the log panel.
"""
import platform
import subprocess
import sys

from textual import work
from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.widgets import Button, Checkbox, DataTable, Footer, Header, RichLog, Select, Static

import Setup


class SetupApp(App):
    TITLE = "Kans3D Setup"
    CSS = """
    #summary { padding: 0 1; color: $text-muted; }
    #deps { height: 1fr; min-height: 8; }
    #options { height: auto; padding: 0 1; }
    #options > * { margin-right: 2; }
    #options Select { width: 24; }
    #buttons { height: auto; padding: 0 1; }
    #buttons Button { margin-right: 1; }
    #log { height: 10; border: round $primary-darken-2; }
    """
    BINDINGS = [
        ("space", "toggle", "Toggle install"),
        ("r", "recheck", "Re-check"),
        ("i", "install", "Install selected"),
        ("c", "configure", "Configure"),
        ("q", "quit", "Quit"),
    ]

    def __init__(self, tests=False, build=False, preset=None):
        super().__init__()
        self.windows = platform.system() == "Windows"
        self.manager = Setup.package_manager()
        self.default_preset = preset or ("windows-msvc" if self.windows else "linux-gcc")
        self.initial_tests, self.initial_build = tests, build
        self.rows = []  # (dependency, ok, detail), in table order
        self.selected = set()  # names of missing dependencies chosen for install
        self.busy = False

    def compose(self) -> ComposeResult:
        yield Header()
        yield Static(id="summary")
        yield DataTable(id="deps", cursor_type="row", zebra_stripes=True)
        with Horizontal(id="options"):
            presets = ["windows-msvc", "windows-clangd"] if self.windows else ["linux-gcc", "linux-clang"]
            yield Select([(p, p) for p in presets], value=self.default_preset, allow_blank=False, id="preset")
            yield Checkbox("Include test dependencies", self.initial_tests, id="tests")
            yield Checkbox("Build KansEditor after configure", self.initial_build, id="build")
        with Horizontal(id="buttons"):
            yield Button("Install selected", id="install", variant="primary")
            yield Button("Configure", id="configure", variant="success")
            yield Button("Re-check", id="recheck")
            yield Button("Quit", id="quit", variant="error")
        yield RichLog(id="log", markup=True, wrap=True)
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one(DataTable)
        table.add_columns("Install", "Status", "Dependency", "Details")
        self.action_recheck()
        self.log_line("[dim]Space toggles a missing dependency. Install runs in your terminal (sudo/winget may ask "
                      "for a password or elevation), then returns here. Configure runs cmake and streams its output "
                      "below.[/]")

    # ------------------------------------------------------------ state

    def dependencies(self):
        tests = self.query_one("#tests", Checkbox).value
        found = Setup.windows_dependencies() if self.windows else Setup.linux_dependencies()
        return [d for d in found if tests or not d.tests_only]

    def missing(self):
        return [d for d, ok, _ in self.rows if not ok]

    def required_missing(self):
        return [d for d, ok, _ in self.rows if not ok and d.required]

    def install_commands(self):
        if self.manager is None:
            return []
        packages = []
        for dependency in self.missing():
            package = dependency.packages.get(self.manager)
            if dependency.name in self.selected and package:
                for item in [package] if self.manager == "winget" else package.split():
                    if item not in packages:
                        packages.append(item)
        return Setup.install_commands(self.manager, packages) if packages else []

    def refresh_table(self):
        table = self.query_one(DataTable)
        cursor = table.cursor_row
        table.clear()
        for dependency, ok, detail in self.rows:
            # Escaped: "[x]" would otherwise be read as a markup tag.
            mark = "" if ok else ("\\[x]" if dependency.name in self.selected else "\\[ ]")
            status = "[green]OK[/]" if ok else ("[red]MISSING[/]" if dependency.required else "[yellow]optional[/]")
            if not ok and dependency.note:
                detail = f"{detail} — {dependency.note}"
            table.add_row(mark, status, dependency.name, detail, key=dependency.name)
        if self.rows:
            table.move_cursor(row=min(cursor, len(self.rows) - 1))
        self.update_controls()

    def update_controls(self):
        missing, required = self.missing(), self.required_missing()
        submodules_ok, submodules = Setup.check_submodules()
        summary = (f"{platform.system()} {platform.machine()} · package manager: {self.manager or 'none found'} · "
                   f"submodules: {submodules} · ")
        summary += "all dependencies found" if not missing else (
            f"{len(required)} required and {len(missing) - len(required)} optional missing")
        self.query_one("#summary", Static).update(summary)
        self.query_one("#install", Button).disabled = self.busy or not self.install_commands()
        self.query_one("#configure", Button).disabled = self.busy or bool(required)
        self.query_one("#recheck", Button).disabled = self.busy

    def log_line(self, text):
        self.query_one(RichLog).write(text)

    # ------------------------------------------------------------ actions

    def action_recheck(self):
        self.rows = []
        for dependency in self.dependencies():
            ok, detail = dependency.check()
            self.rows.append((dependency, ok, detail))
        # Missing first (required before optional), so they are visible without scrolling.
        self.rows.sort(key=lambda row: (row[1], not row[0].required))
        # Missing dependencies start selected; a later re-check keeps the user's choices.
        names = {d.name for d in self.missing()}
        if not self.selected & names:
            self.selected = set(names)
        self.selected &= names
        self.refresh_table()

    def action_toggle(self):
        table = self.query_one(DataTable)
        if not self.rows or table.cursor_row >= len(self.rows):
            return
        dependency, ok, _ = self.rows[table.cursor_row]
        if ok:
            return
        self.selected ^= {dependency.name}
        self.refresh_table()

    def on_data_table_row_selected(self, event: DataTable.RowSelected) -> None:
        self.action_toggle()

    def on_checkbox_changed(self, event: Checkbox.Changed) -> None:
        if event.checkbox.id == "tests":
            self.action_recheck()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        handlers = {"install": self.action_install, "configure": self.action_configure,
                    "recheck": self.action_recheck, "quit": self.exit}
        handlers[event.button.id]()

    def action_install(self):
        commands = self.install_commands()
        if not commands or self.busy:
            return
        # sudo and winget need the real terminal for passwords and elevation prompts.
        failed = None
        with self.suspend():
            print("\nKans3D setup — running install commands\n")
            for command in commands:
                print(f"$ {command}")
                if subprocess.call(command, shell=True) != 0:
                    failed = command
                    break
            if self.windows and not failed:
                print("\nOpen a new terminal afterwards so PATH and VULKAN_SDK changes take effect.")
            input("\nPress Enter to return to Kans3D Setup...")
        for command in commands:
            self.log_line(f"[dim]$ {command}[/]")
        self.log_line(f"[red]Failed:[/] {failed}" if failed else "[green]Install commands finished.[/]")
        self.action_recheck()

    def action_configure(self):
        if self.busy or self.required_missing():
            return
        steps = []
        if not Setup.check_submodules()[0]:
            steps.append(["git", "submodule", "update", "--init", "--recursive"])
        preset = self.query_one("#preset", Select).value
        steps.append(["cmake", "--preset", preset])
        if self.query_one("#build", Checkbox).value:
            steps.append(["cmake", "--build", f"build/{preset}", "--config", "Debug", "--target", "KansEditor",
                          "--parallel", str(Setup.os.cpu_count() or 2)])
        self.busy = True
        self.update_controls()
        self.run_steps(steps)

    @work(thread=True, exclusive=True)
    def run_steps(self, steps):
        ok = True
        for step in steps:
            self.call_from_thread(self.log_line, f"[bold]$ {' '.join(step)}[/]")
            process = subprocess.Popen(step, cwd=Setup.ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True, errors="replace")
            for line in process.stdout:
                self.call_from_thread(self.log_line, line.rstrip())
            if process.wait() != 0:
                self.call_from_thread(self.log_line, f"[red]Failed with exit code {process.returncode}[/]")
                ok = False
                break
        self.call_from_thread(self.finish_steps, ok)

    def finish_steps(self, ok):
        self.busy = False
        self.log_line("[green]Done.[/]" if ok else "[red]Stopped.[/]")
        self.update_controls()


def run(tests=False, build=False, preset=None):
    SetupApp(tests=tests, build=build, preset=preset).run()


if __name__ == "__main__":
    run()
