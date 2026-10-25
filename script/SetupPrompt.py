"""Inline choice prompts for Setup.py (prompt_toolkit, vendored as pure-Python wheels in script/third_party).

Like a CLI question: the options are drawn under the question, Tab / Shift+Tab / arrow keys move,
Space toggles (multi-select), Enter confirms, digits pick an option, and a mouse click selects.
"""
import sys

from prompt_toolkit.application import Application
from prompt_toolkit.formatted_text import FormattedText
from prompt_toolkit.key_binding import KeyBindings
from prompt_toolkit.layout import Layout, Window
from prompt_toolkit.layout.controls import FormattedTextControl
from prompt_toolkit.mouse_events import MouseEventType
from prompt_toolkit.styles import Style


def _symbols():
    encoding = getattr(sys.stdout, "encoding", None) or "ascii"
    try:
        "❯✔◉○".encode(encoding)
        return {"cursor": "❯", "done": "✔", "on": "◉", "off": "○"}
    except (UnicodeEncodeError, LookupError):
        return {"cursor": ">", "done": "*", "on": "[x]", "off": "[ ]"}


STYLE = Style.from_dict({
    "question": "bold",
    "hint": "#888888",
    "cursor": "#5fafff bold",
    "selected": "#5fafff bold",
    "label": "",
    "description": "#888888",
    "disabled": "#606060",
    "confirm": "#87d787 bold",
})


class Option:
    def __init__(self, label, description="", enabled=True, value=None):
        self.label, self.description, self.enabled = label, description, enabled
        self.value = label if value is None else value


def choose(question, options, multi=False, checked=(), hint=None):
    """Returns the chosen option's value (single), a list of values (multi), or None when cancelled."""
    symbols = _symbols()
    rows = list(options)
    if multi:
        rows.append(Option("Confirm", "", True, value="__confirm__"))
    state = {"index": next((i for i, o in enumerate(rows) if o.enabled), 0),
             "checked": {o.value for o in options if o.value in set(checked)}}

    def move(step):
        index = state["index"]
        for _ in range(len(rows)):
            index = (index + step) % len(rows)
            if rows[index].enabled:
                state["index"] = index
                return

    def finish(app):
        if multi:
            app.exit(result=[o.value for o in options if o.value in state["checked"]])
        else:
            app.exit(result=rows[state["index"]].value)

    def activate(app, index):
        option = rows[index]
        if not option.enabled:
            return
        state["index"] = index
        if multi and option.value != "__confirm__":
            state["checked"] ^= {option.value}
        else:
            finish(app)

    def clicked(index):
        def handler(event):
            if event.event_type == MouseEventType.MOUSE_UP:
                activate(app_ref[0], index)
            else:
                return NotImplemented
        return handler

    def render():
        keys = "Tab/↑↓ move · Space toggle · Enter confirm · click" if multi else "Tab/↑↓ move · Enter select · click"
        if symbols["cursor"] == ">":
            keys = keys.replace("↑↓", "arrows")
        fragments = [("class:question", f"? {question}"), ("class:hint", f"   {hint or keys}\n")]
        for index, option in enumerate(rows):
            current = index == state["index"]
            handler = clicked(index)
            cursor = f" {symbols['cursor']} " if current else "   "
            fragments.append(("class:cursor", cursor, handler))
            if option.value == "__confirm__":
                count = len(state["checked"])
                fragments.append(("class:confirm" if current else "class:label", f"Confirm ({count} selected)", handler))
                fragments.append(("", "\n"))
                continue
            number = f"{index + 1}. " if index < 9 else "   "
            if multi:
                mark = symbols["on"] if option.value in state["checked"] else symbols["off"]
                number = f"{mark} "
            label_style = "class:disabled" if not option.enabled else ("class:selected" if current else "class:label")
            fragments.append((label_style, number + option.label, handler))
            if option.description:
                style = "class:disabled" if not option.enabled else "class:description"
                fragments.append((style, f"  {option.description}", handler))
            fragments.append(("", "\n"))
        return FormattedText(fragments)

    bindings = KeyBindings()

    @bindings.add("up")
    @bindings.add("s-tab")
    @bindings.add("k")
    def _(event):
        move(-1)

    @bindings.add("down")
    @bindings.add("tab")
    @bindings.add("j")
    def _(event):
        move(1)

    @bindings.add(" ")
    def _(event):
        if multi:
            activate(event.app, state["index"])

    @bindings.add("enter")
    def _(event):
        if multi and rows[state["index"]].value != "__confirm__":
            finish(event.app)
        else:
            activate(event.app, state["index"])

    for digit in range(1, min(len(options), 9) + 1):
        @bindings.add(str(digit))
        def _(event, index=digit - 1):
            activate(event.app, index)

    @bindings.add("escape")
    @bindings.add("c-c")
    def _(event):
        event.app.exit(result=None)

    app = Application(layout=Layout(Window(FormattedTextControl(render, focusable=True), wrap_lines=True)),
                      key_bindings=bindings, style=STYLE, mouse_support=True, full_screen=False,
                      erase_when_done=True)
    app_ref = [app]
    result = app.run()

    # Leave a one-line record of the answer, like an answered CLI question.
    if result is None:
        answer = "cancelled"
    elif multi:
        labels = [o.label for o in options if o.value in result]
        answer = ", ".join(labels) if labels else "none"
    else:
        answer = next(o.label for o in rows if o.value == result)
    print(f"{symbols['done']} {question} · {answer}")
    return result
