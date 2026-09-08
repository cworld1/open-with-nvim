# Open with Nvim

## About

A helper that helps open with nvim on Windows. This will open nvim with specific file by Windows Terminal, so this program doesn't have GUI.

https://github.com/user-attachments/assets/04636a26-dd26-4345-a90b-63ff2cdf147b

## Features

You can:

- Click to open a nvim window.
- Drag files to exe to open it.
- Set open with this exe on any files.
- Open a folder as Neovim's working directory; opening a file uses its containing folder.

## Usage

Run the executable once to generate open-with-nvim.ini in the same directory, then edit that file with your preferred settings.

## Local Development

Prerequisites:

- A Windows-targeting C23 compiler (MinGW GCC or Clang), plus `windres`.
- CMake 3.21 or newer and GNU Make for the CMake build below.

Clone the repository and navigate to the project directory:

```bash
git clone https://github.com/cworld1/open-with-nvim.git
cd open-with-nvim
```

Configure and build from a shell where these tools are on `PATH`:

```bash
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

The executable is `build/nvim.exe`; the compilation database for editor tooling
is `build/compile_commands.json`. The runtime INI file is created beside the
executable, so copy your existing INI there if you want to reuse its settings.
Use a MinGW toolchain rather than the MSYS compiler, which targets the MSYS runtime.

Alternatively, build `nvim.exe` in the repository root with the existing script:

```bash
./build.sh
```

## Contributions

As the author is only a beginner in learning it, there are obvious mistakes in his notes. Readers are also invited to make a lot of mistakes. In addition, you are welcome to use PR or Issues to improve them.

## License

This project is licensed under the GPL-3 License.
