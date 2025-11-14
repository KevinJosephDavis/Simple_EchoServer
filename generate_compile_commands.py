import json
import os

#配置参数
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))  #根目录
CXX = "/usr/bin/g++"  #g++编译器路径（默认是这个，不确定可以输 which g++ 查看）

#C++风格编译选项
CPP_FLAGS = [
    "-c", "-g",  #-c：编译生成目标文件；-g：保留调试信息
    "-std=c++20",  #C++标准
    "-pthread",    #线程库依赖（必须加）
    "-I/usr/include/c++/11",  #头文件路径
    "-I/usr/include/x86_64-linux-gnu/c++/11",
    "-I/usr/include"
]

#C风格编译选项（C11 标准，适配C代码）
C_FLAGS = [
    "-c", "-g",
    "-std=c11",  #C标准
    "-pthread",  #C风格pthread 依赖
    "-I/usr/include"
]

def generate_compile_commands():
    compile_commands = []  #存储所有配置条目

    #遍历根目录下的所有文件和子目录
    for root, dirs, files in os.walk(ROOT_DIR):
        #只处理.cpp 文件（C和C++都用.cpp后缀，脚本自动区分）
        for file in files:
            if file.endswith(".cpp"):
                #1.获取源码文件的绝对路径（比如/xxx/cpp_style/Multithread_EchoServer/server.cpp）
                src_file = os.path.join(root, file)
                #2.生成可执行文件名称（去掉.cpp后缀，比如 echo_server -> server）
                exe_name = os.path.splitext(file)[0]  # 比如"server.cpp" -> "server"
                #3.可执行文件输出路径（和源码文件在同一目录）
                exe_file = os.path.join(root, exe_name)

                #4.判断是C风格还是C++风格（根据目录名"c_style"区分）
                if "c_style" in root:
                    flags = C_FLAGS  #目录含"c_style"用C编译选项
                else:
                    flags = CPP_FLAGS  #否则用C++编译选项

                #5.构造完整的编译命令（和手动敲的g++命令一致）
                compile_cmd = [CXX] + flags + ["-o", exe_file, src_file]

                #6.生成当前文件的配置条目
                entry = {
                    "arguments": compile_cmd,  #编译命令列表
                    "directory": ROOT_DIR,     #项目根目录
                    "file": src_file,          #源码文件绝对路径
                    "output": exe_file         #输出文件绝对路径
                }

                #添加到配置列表
                compile_commands.append(entry)

    #7.把配置列表写入compile_commands.json文件
    output_file = os.path.join(ROOT_DIR, "compile_commands.json")
    with open(output_file, "w", encoding="utf-8") as f:
        #indent=2：格式化输出，方便查看；ensure_ascii=False：支持中文目录
        json.dump(compile_commands, f, indent=2, ensure_ascii=False)

    print(f"配置文件生成成功！")
    print(f"生成路径：{output_file}")
    print(f"共生成 {len(compile_commands)} 个文件的配置条目")

if __name__ == "__main__":
    generate_compile_commands()