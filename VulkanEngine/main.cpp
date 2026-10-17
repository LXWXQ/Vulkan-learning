#include "src/Core/Application.h"
#include "src/Core/JobSystem.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <string>

// ---------------------------------------------------------------------------
// 命令行参数
//   （无）                        正常跑窗口，关闭窗口退出
//   --selftest [N]                跑 N 帧（默认 600）后退出，用于验证层回归
//   --no-bloom                    自检时关掉 Bloom
//   --no-ssao                     自检时关掉 SSAO
//
// 为什么需要自检开关：
//   "关掉某个 Pass"是最容易漏验证层错误的分支 —— 中间目标整帧没人写
//   （布局一直停在 UNDEFINED），后面的 Pass 却仍然声明要采样它。
//   而 ImGui 的复选框没法在无人值守的验证流程里点，所以把开关做成
//   命令行参数，让这些分支也能进"验证层 0 字节"的回归检查。
//
// 这两个开关走的是和 UI 复选框完全相同的代码路径（EngineSettings）。
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // stdout 关闭缓冲：日志重定向到文件时也能实时看到加载进度，
    // 而不是等到崩溃/退出才一次性落盘（排查卡死时非常有用）。
    setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef _WIN32
    system("chcp 65001 > nul");
#endif

    bool selfTest = false;
    bool noBloom = false;
    bool noSSAO = false;
    bool jobTest = false;
    int  selfTestFrames = 600;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--selftest")
        {
            selfTest = true;
            if (i + 1 < argc)
            {
                try { selfTestFrames = std::stoi(argv[i + 1]); }
                catch (...) { /* 参数不是数字就沿用默认帧数 */ }
            }
        }
        else if (arg == "--no-bloom") noBloom = true;
        else if (arg == "--no-ssao")  noSSAO = true;
        else if (arg == "--jobtest")  jobTest = true;
    }

    try
    {
        if (jobTest)
        {
            JobSystem jobSystem;
            return jobSystem.selfTest() ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        FirstApp app;

        if (selfTest)
        {
            if (noBloom) app.setBloomEnabled(false);
            if (noSSAO)  app.setSSAOEnabled(false);
            return app.runHeadlessSelfTest(selfTestFrames);
        }

        app.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "致命错误: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
