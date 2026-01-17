
mkdir Intermediate
mkdir Intermediate\Downloads
curl -o Intermediate/Downloads/dxc.zip -L https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/dxc_2025_07_14.zip
curl -o Intermediate/Downloads/D3D12.zip -L https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.618.1
curl -o Intermediate/Downloads/WinPixEventRuntime.zip -L https://www.nuget.org/api/v2/package/WinPixEventRuntime/1.0.240308001
curl -o Intermediate/Downloads/DLSS.zip -L https://github.com/NVIDIA/DLSS/archive/refs/tags/v310.5.0.zip
curl -o Intermediate/Downloads/XeSS.zip -L https://github.com/intel/xess/releases/download/v2.1.1/XeSS_SDK_2.1.1.zip


mkdir SDK\dxc
tar -xf Intermediate/Downloads/dxc.zip -C SDK/dxc

mkdir Intermediate\Downloads\D3D12
tar -xf Intermediate/Downloads/D3D12.zip -C Intermediate/Downloads/D3D12

mkdir Intermediate\Downloads\WinPixEventRuntime
tar -xf Intermediate/Downloads/WinPixEventRuntime.zip -C Intermediate/Downloads/WinPixEventRuntime

mkdir Intermediate\Downloads\DLSS
tar -xf Intermediate/Downloads/DLSS.zip -C Intermediate/Downloads/DLSS

mkdir Intermediate\Downloads\XeSS
tar -xf Intermediate/Downloads/XeSS.zip -C Intermediate/Downloads/XeSS


mkdir SDK\D3D12
mkdir SDK\D3D12\bin
mkdir SDK\D3D12\include
xcopy /y Intermediate\Downloads\D3D12\LICENSE.txt SDK\D3D12
xcopy /y Intermediate\Downloads\D3D12\LICENSE-CODE.txt SDK\D3D12
xcopy /y Intermediate\Downloads\D3D12\build\native\bin\x64\*.dll SDK\D3D12\bin
xcopy /y Intermediate\Downloads\D3D12\build\native\include\*.h SDK\D3D12\include

mkdir SDK\WinPixEventRuntime
mkdir SDK\WinPixEventRuntime\bin
mkdir SDK\WinPixEventRuntime\include
xcopy /y Intermediate\Downloads\WinPixEventRuntime\license.txt SDK\WinPixEventRuntime
xcopy /y Intermediate\Downloads\WinPixEventRuntime\ThirdPartyNotices.txt SDK\WinPixEventRuntime
xcopy /y Intermediate\Downloads\WinPixEventRuntime\bin\x64\WinPixEventRuntime.dll SDK\WinPixEventRuntime\bin
xcopy /y Intermediate\Downloads\WinPixEventRuntime\bin\x64\WinPixEventRuntime.lib SDK\WinPixEventRuntime\bin
xcopy /y Intermediate\Downloads\WinPixEventRuntime\Include\WinPixEventRuntime\*.h SDK\WinPixEventRuntime\include

mkdir SDK\DLSS
mkdir SDK\DLSS\bin
mkdir SDK\DLSS\include
xcopy /y Intermediate\Downloads\DLSS\DLSS-310.5.0\LICENSE.txt SDK\DLSS
xcopy /y Intermediate\Downloads\DLSS\DLSS-310.5.0\doc\DLSS_Programming_Guide_Release.pdf SDK\DLSS
xcopy /y Intermediate\Downloads\DLSS\DLSS-310.5.0\lib\Windows_x86_64\x64\nvsdk_ngx_d.lib SDK\DLSS\bin
xcopy /y Intermediate\Downloads\DLSS\DLSS-310.5.0\lib\Windows_x86_64\x64\nvsdk_ngx_d_dbg_iterator0.lib SDK\DLSS\bin
xcopy /y Intermediate\Downloads\DLSS\DLSS-310.5.0\lib\Windows_x86_64\rel\nvngx_dlss.dll SDK\DLSS\bin
xcopy /y Intermediate\Downloads\DLSS\DLSS-310.5.0\include\nvsdk_ngx.h SDK\DLSS\include
xcopy /y Intermediate\Downloads\DLSS\DLSS-310.5.0\include\nvsdk_ngx_defs.h SDK\DLSS\include
xcopy /y Intermediate\Downloads\DLSS\DLSS-310.5.0\include\nvsdk_ngx_params.h SDK\DLSS\include
xcopy /y Intermediate\Downloads\DLSS\DLSS-310.5.0\include\nvsdk_ngx_helpers.h SDK\DLSS\include

mkdir SDK\XeSS
mkdir SDK\XeSS\bin
mkdir SDK\XeSS\include
xcopy /y Intermediate\Downloads\XeSS\LICENSE.txt SDK\XeSS
xcopy /y Intermediate\Downloads\XeSS\third-party-programs.txt SDK\XeSS
xcopy /y Intermediate\Downloads\XeSS\lib\libxess.lib SDK\XeSS\bin
xcopy /y Intermediate\Downloads\XeSS\bin\libxess.dll SDK\XeSS\bin
xcopy /y Intermediate\Downloads\XeSS\inc\xess\xess.h SDK\XeSS\include
xcopy /y Intermediate\Downloads\XeSS\inc\xess\xess_d3d12.h SDK\XeSS\include


rmdir Intermediate\Downloads\ /s /q
