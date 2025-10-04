
mkdir Intermediate\Downloads
curl -o Intermediate/Downloads/dxc.zip -L https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/dxc_2025_07_14.zip
curl -o Intermediate/Downloads/D3D12.zip -L https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.618.1

mkdir SDK\dxc
tar -xf Intermediate/Downloads/dxc.zip -C SDK/dxc

mkdir Intermediate\Downloads\D3D12
tar -xf Intermediate/Downloads/D3D12.zip -C Intermediate/Downloads/D3D12

mkdir SDK\D3D12
mkdir SDK\D3D12\bin
mkdir SDK\D3D12\include
xcopy Intermediate\Downloads\D3D12\LICENSE.txt SDK\D3D12 /y
xcopy Intermediate\Downloads\D3D12\LICENSE-CODE.txt SDK\D3D12 /y
xcopy Intermediate\Downloads\D3D12\build\native\bin\x64\*.dll SDK\D3D12\bin /y
xcopy Intermediate\Downloads\D3D12\build\native\include\*.h SDK\D3D12\include /y

rmdir Intermediate\Downloads\ /s /q
