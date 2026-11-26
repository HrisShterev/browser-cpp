import subprocess
import tempfile
import os
import re
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

INSTRUMENTER_BIN = os.path.expanduser("~/browser-cpp/cpp_tracer/build/cpp-instrumenter")

class CodeRequest(BaseModel):
    code: str

def split_functions_and_statements(code: str):
    """Отделя дефинициите на функции (напр. void swap_numbers(...)) от обикновените редове."""
    lines = code.splitlines()
    func_lines = []
    main_lines = []
    
    brace_depth = 0
    in_function = False
    func_pattern = re.compile(r'^\s*(?:void|int|double|float|char|bool|long|short|auto|std::\w+)\s+\w+\s*\([^;]*\)\s*\{?\s*$')

    for line in lines:
        if not in_function and brace_depth == 0 and func_pattern.match(line):
            in_function = True
            func_lines.append(line)
            brace_depth += line.count('{') - line.count('}')
            continue

        if in_function:
            func_lines.append(line)
            brace_depth += line.count('{') - line.count('}')
            if brace_depth <= 0 and '{' in "".join(func_lines):
                in_function = False
                brace_depth = 0
        else:
            main_lines.append(line)

    return "\n".join(func_lines), "\n".join(main_lines)

@app.post("/instrument")
def instrument_code(req: CodeRequest):
    funcs_code, stmts_code = split_functions_and_statements(req.code)
    wrapped_code = f"{funcs_code}\nvoid __student_cell__() {{\n{stmts_code}\n}}\n"

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

        # Премахваме #include и обвивката void __student_cell__() { ... } за xeus-cpp
        lines = instrumented.splitlines()
        out_lines = []
        inside_student_cell = False
        cell_brace_depth = 0

        for line in lines:
            if line.startswith('#include "tracer_runtime.hpp"'):
                continue
            if "void __student_cell__()" in line:
                inside_student_cell = True
                cell_brace_depth += line.count('{') - line.count('}')
                continue
            if inside_student_cell:
                cell_brace_depth += line.count('{') - line.count('}')
                if cell_brace_depth <= 0:
                    inside_student_cell = False
                    continue
            out_lines.append(line)

        final_code = (
            '#include "tracer_runtime.hpp"\n'
            'TracerRuntime::clear();\n'
            + "\n".join(out_lines)
            + "\nSHOW_MEMORY();\n"
        )
        return {"instrumented_code": final_code, "stderr": res.stderr}
    finally:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)
