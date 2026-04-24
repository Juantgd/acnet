## 项目构建

```bash
# build and compile
cmake -S . -B build
cmake --build build -j$(nproc)

# run
cd build/bin
./acnet -h
./acnet
```
