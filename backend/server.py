import subprocess
import tempfile
import os
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

app = FastAPI()

# Позволяваме заявки от JupyterLite (порт 8000)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

INSTRUMENTER_BIN = os.path.expanduser("~/browser-cpp/cpp_tracer/build/cpp-instrumenter")

class CodeRequest(BaseModel):
    code: str

@app.post("/instrument")
def instrument_code(req: CodeRequest):
    # Учениците в Jupyter пишат код директно в клетка (без int main()),
    # а Clang AST изисква функция, затова го обвиваме във временна функция
    wrapped_code = f"void __student_cell__() {{\n{req.code}\n}}\n"

    with tempfile.NamedTemporaryFile(suffix=".cpp", mode="w", delete=False) as tmp:
        tmp.write(wrapped_code)
        tmp_path = tmp.name

    try:
        res = subprocess.run(
            [INSTRUMENTER_BIN, tmp_path, "--"],
            capture_output=True,
            text=True,
            timeout=5
        )
        instrumented = res.stdout

        # Премахваме обвивката void __student_cell__() { ... } за изпълнение в xeus-cpp
        lines = instrumented.splitlines()
        body_lines = []
        inside = False
        for line in lines:
            if line.startswith('#include "tracer_runtime.hpp"'):
                continue
            if "void __student_cell__()" in line:
                inside = True
                continue
            if inside:
                body_lines.append(line)

        # Махаме последната затваряща скоба '}'
        if body_lines and body_lines[-1].strip() == "}":
            body_lines.pop()

        final_code = (
            '#include "tracer_runtime.hpp"\n'
            'TracerRuntime::clear();\n'
            + "\n".join(body_lines)
            + "\nSHOW_MEMORY();\n"
        )
        return {"instrumented_code": final_code, "stderr": res.stderr}
    finally:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)
