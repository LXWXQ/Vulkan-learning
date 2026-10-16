#include "Core/EngineConfig.h"
#include "AssetPaths.h"   // SENTINEL_CONFIG_PATH（CMake 配置阶段生成）

// nlohmann/json 来自 3rdParty/tinygltf（tinygltf 拉取时带下来的），
// 不额外引依赖、也不需要联网。它本身是 header-only 的第三方代码，
// MSVC 的 /W4 会在里面刷出一堆我们用不到的警告，所以用 pragma 屏蔽一段。
#pragma warning(push)
#pragma warning(disable : 4244 4267 4996 5054 4127 4324)
#include <json.hpp>
#pragma warning(pop)

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdio>
#include <vector>
#include <exception>

namespace
{
	using nlohmann::json;

	std::string g_lastStatus = "(no config I/O yet)";

	void setStatus(const std::string& s)
	{
		g_lastStatus = s;
	}

	// ---- glm 向量 <-> json 数组 ----
	// glm 和 json 之间没有内置转换，写成模板省得为 vec2/vec3/vec4 各来一遍。
	template <typename VecT>
	VecT readVec(const json& node, const char* key, const VecT& fallback)
	{
		const auto it = node.find(key);
		if (it == node.end() || !it->is_array() || it->size() != static_cast<size_t>(VecT::length()))
			return fallback;

		VecT out;
		for (size_t i = 0; i < static_cast<size_t>(VecT::length()); ++i)
		{
			const json& e = (*it)[i];
			// 整数形式的 1 也算合法（手写配置文件时很容易写成 1 而不是 1.0）
			if (!e.is_number()) return fallback;
			out[static_cast<glm::length_t>(i)] = e.get<float>();
		}
		return out;
	}

	template <typename VecT>
	void writeVec(json& node, const char* key, const VecT& v)
	{
		json arr = json::array();
		for (size_t i = 0; i < static_cast<size_t>(VecT::length()); ++i)
			arr.push_back(v[static_cast<glm::length_t>(i)]);
		node[key] = std::move(arr);
	}

	/// 从节点里取一个字段；类型不对时保留 fallback 并记一条说明。
	/// 为什么不用 node.value(key, fallback)：那个在类型不匹配时会抛异常，
	/// 而"用户手改配置文件写错一个字段"不该让引擎起不来。
	template <typename T>
	T read(const json& node, const char* key, const T& fallback, std::vector<std::string>& warnings)
	{
		const auto it = node.find(key);
		if (it == node.end()) return fallback;
		try
		{
			return it->get<T>();
		}
		catch (const std::exception&)
		{
			warnings.push_back(std::string(key) + ": bad type, keeping default");
			return fallback;
		}
	}

	json settingsToJson(const EngineSettings& s)
	{
		json j;
		j["windowTitle"] = s.windowTitle;

		json ssao;
		ssao["enabled"] = s.ssaoEnabled;
		ssao["radius"] = s.ssaoRadius;
		ssao["bias"] = s.ssaoBias;
		ssao["kernelSize"] = s.ssaoKernelSize;
		j["ssao"] = std::move(ssao);

		json bloom;
		bloom["enabled"] = s.bloomEnabled;
		bloom["threshold"] = s.bloomThreshold;
		bloom["knee"] = s.bloomKnee;
		bloom["intensity"] = s.bloomIntensity;
		j["bloom"] = std::move(bloom);

		json light;
		writeVec(light, "direction", s.directionalLightDir);
		writeVec(light, "color", s.directionalLightColor);
		light["exposure"] = s.exposure;
		j["lighting"] = std::move(light);

		json planned;
		planned["ssrEnabled"] = s.ssrEnabled;
		planned["shadowEnabled"] = s.shadowEnabled;
		j["planned"] = std::move(planned);

		return j;
	}

	// 只覆盖 out 里确实存在的字段，缺失的保持 out 的当前值
	void jsonToSettings(const json& j, EngineSettings& out, std::vector<std::string>& warnings)
	{
		out.windowTitle = read<std::string>(j, "windowTitle", out.windowTitle, warnings);

		if (j.contains("ssao") && j["ssao"].is_object())
		{
			const json& n = j["ssao"];
			out.ssaoEnabled = read(n, "enabled", out.ssaoEnabled, warnings);
			out.ssaoRadius = read(n, "radius", out.ssaoRadius, warnings);
			out.ssaoBias = read(n, "bias", out.ssaoBias, warnings);
			out.ssaoKernelSize = read(n, "kernelSize", out.ssaoKernelSize, warnings);
		}

		if (j.contains("bloom") && j["bloom"].is_object())
		{
			const json& n = j["bloom"];
			out.bloomEnabled = read(n, "enabled", out.bloomEnabled, warnings);
			out.bloomThreshold = read(n, "threshold", out.bloomThreshold, warnings);
			out.bloomKnee = read(n, "knee", out.bloomKnee, warnings);
			out.bloomIntensity = read(n, "intensity", out.bloomIntensity, warnings);
		}

		if (j.contains("lighting") && j["lighting"].is_object())
		{
			const json& n = j["lighting"];
			out.directionalLightDir = readVec(n, "direction", out.directionalLightDir);
			out.directionalLightColor = readVec(n, "color", out.directionalLightColor);
			out.exposure = read(n, "exposure", out.exposure, warnings);
		}

		if (j.contains("planned") && j["planned"].is_object())
		{
			const json& n = j["planned"];
			out.ssrEnabled = read(n, "ssrEnabled", out.ssrEnabled, warnings);
			out.shadowEnabled = read(n, "shadowEnabled", out.shadowEnabled, warnings);
		}
	}

	json cameraToJson(const CameraState& c)
	{
		json j;
		writeVec(j, "position", c.position);
		writeVec(j, "rotation", c.rotation);
		return j;
	}

	void jsonToCamera(const json& j, CameraState& out)
	{
		out.position = readVec(j, "position", out.position);
		out.rotation = readVec(j, "rotation", out.rotation);
	}

	/// 读整个配置文件。返回 false 表示"没有可用的文件"，原因写进 outWhy。
	bool readRoot(json& outRoot, std::string& outWhy)
	{
		std::ifstream file(EngineConfig::getConfigPath());
		if (!file.is_open())
		{
			outWhy = "config file not found";
			return false;
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		const std::string text = buffer.str();
		if (text.empty())
		{
			outWhy = "config file is empty";
			return false;
		}

		// 不使用会抛异常的 parse：别人手改坏的配置不应该让引擎起不来
		json root = json::parse(text, nullptr, false);
		if (root.is_discarded())
		{
			outWhy = "config file is not valid JSON";
			return false;
		}
		if (!root.is_object())
		{
			outWhy = "config root is not an object";
			return false;
		}

		outRoot = std::move(root);
		return true;
	}

	/// 原子写：先写 .tmp 再 rename。
	/// 直接覆盖写的话，写到一半崩溃就会留下语法坏掉的配置，下次启动直接报错。
	bool writeRoot(const json& root)
	{
		const std::string path = EngineConfig::getConfigPath();
		const std::string tmp = path + ".tmp";
		const std::string text = root.dump(2) + "\n";

		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out.is_open())
			{
				setStatus("save failed: cannot write " + tmp);
				std::cerr << "[Config] " << g_lastStatus << "\n";
				return false;
			}
			out << text;
			if (!out.good())
			{
				setStatus("save failed: write error");
				std::cerr << "[Config] " << g_lastStatus << "\n";
				return false;
			}
		}

		std::remove(path.c_str());   // Windows 的 rename 不覆盖已存在文件
		if (std::rename(tmp.c_str(), path.c_str()) != 0)
		{
			setStatus("save failed: rename " + tmp + " -> " + path);
			std::cerr << "[Config] " << g_lastStatus << "\n";
			return false;
		}
		return true;
	}
}

const std::string& EngineConfig::getConfigPath()
{
	static const std::string path = SENTINEL_CONFIG_PATH;
	return path;
}

std::string EngineConfig::getConfigFileName()
{
	// 状态栏只显示文件名：绝对路径可能有五十多个字符，会把 ImGui 面板撑得很宽。
	static const std::string name = []() -> std::string {
		const std::string& full = getConfigPath();
		const size_t slash = full.find_last_of("/\\");
		return (slash == std::string::npos) ? full : full.substr(slash + 1);
	}();
	return name;
}

const std::string& EngineConfig::getLastStatus()
{
	return g_lastStatus;
}

bool EngineConfig::load(EngineSettings& out)
{
	json root;
	std::string why;
	if (!readRoot(root, why))
	{
		setStatus(why + " - using compile-time defaults");
		std::cout << "[Config] " << g_lastStatus << " -> " << getConfigPath() << "\n";
		return false;
	}

	// 沿用同一套 save 的嵌套结构：{ "engineSettings": {...}, "camera": {...} }
	if (!root.contains("engineSettings") || !root["engineSettings"].is_object())
	{
		setStatus("no engineSettings node - using compile-time defaults");
		std::cout << "[Config] " << g_lastStatus << "\n";
		return false;
	}

	std::vector<std::string> warnings;
	jsonToSettings(root["engineSettings"], out, warnings);

	setStatus("loaded " + getConfigFileName());
	if (!warnings.empty())
	{
		setStatus("loaded with " + std::to_string(warnings.size()) + " bad field(s), defaults kept");
		for (const std::string& w : warnings)
			std::cout << "[Config] 警告: " << w << "\n";
	}
	std::cout << "[Config] " << g_lastStatus << " -> " << getConfigPath() << "\n";
	return true;
}

bool EngineConfig::load(CameraState& out)
{
	json root;
	std::string why;
	if (!readRoot(root, why))
		return false;   // 状态已由 load(EngineSettings&) 报过，这里不重复刷屏

	if (!root.contains("camera") || !root["camera"].is_object())
		return false;

	jsonToCamera(root["camera"], out);
	return true;
}

bool EngineConfig::save(const EngineSettings& settings)
{
	// 先把已有文件读回来，只替换 engineSettings 节点 ——
	// 否则"保存渲染参数"会把相机位姿一起抹掉（两个 save 重载共用一个文件，
	// 这种互相覆盖是共用文件必须付的代价，不能靠"记得先读一下"来解决）。
	json root;
	std::string why;
	if (!readRoot(root, why))
		root = json::object();

	root["version"] = 1;
	root["engineSettings"] = settingsToJson(settings);

	if (!writeRoot(root))
		return false;

	setStatus("saved render settings -> " + getConfigFileName());
	std::cout << "[Config] " << g_lastStatus << "\n";
	return true;
}

bool EngineConfig::save(const CameraState& camera)
{
	json root;
	std::string why;
	if (!readRoot(root, why))
		root = json::object();

	root["version"] = 1;
	root["camera"] = cameraToJson(camera);

	if (!writeRoot(root))
		return false;

	setStatus("saved camera pose -> " + getConfigFileName());
	std::cout << "[Config] " << g_lastStatus << "\n";
	return true;
}
