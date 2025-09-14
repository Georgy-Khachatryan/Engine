
mkdir Intermediate\Downloads
curl -o Intermediate/Downloads/dxc.zip -L https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/dxc_2025_07_14.zip

mkdir SDK\dxc
tar -xf Intermediate/Downloads/dxc.zip -C SDK/dxc
