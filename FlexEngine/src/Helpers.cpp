#include "stdafx.hpp"

#include "Helpers.hpp"

#include <iomanip> // for setprecision

IGNORE_WARNINGS_PUSH
#include <glm/gtx/matrix_decompose.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
IGNORE_WARNINGS_POP

#include "FlexEngine.hpp" // For FlexEngine::s_CurrentWorkingDirectory
#include "Graphics/Renderer.hpp" // For MAX_TEXTURE_DIM
#include "Platform/Platform.hpp"
#include "Transform.hpp"

// Taken from "AL/al.h":
#define AL_FORMAT_MONO8                           0x1100
#define AL_FORMAT_MONO16                          0x1101
#define AL_FORMAT_STEREO8                         0x1102
#define AL_FORMAT_STEREO16                        0x1103

static const char* SEPARATOR_STR = ", ";

#define set1_ps_hex(x) _mm_castsi128_ps(_mm_set1_epi32(x))

static const __m128 _ps_1 = _mm_set1_ps(1.f);
static const __m128 _ps_0p5 = _mm_set1_ps(0.5f);
static const __m128 _ps_sign_mask = set1_ps_hex(0x80000000);

static const __m128i _pi32_1 = _mm_set1_epi32(1);
static const __m128i _pi32_inv1 = _mm_set1_epi32(~1);
static const __m128i _pi32_2 = _mm_set1_epi32(2);
static const __m128i _pi32_4 = _mm_set1_epi32(4);

static const __m128 _ps_minus_cephes_DP1 = _mm_set1_ps(-0.78515625f);
static const __m128 _ps_minus_cephes_DP2 = _mm_set1_ps(-2.4187564849853515625e-4f);
static const __m128 _ps_minus_cephes_DP3 = _mm_set1_ps(-3.77489497744594108e-8f);
static const __m128 _ps_sincof_p0 = _mm_set1_ps(-1.9515295891E-4f);
static const __m128 _ps_sincof_p1 = _mm_set1_ps(8.3321608736E-3f);
static const __m128 _ps_sincof_p2 = _mm_set1_ps(-1.6666654611E-1f);
static const __m128 _ps_coscof_p0 = _mm_set1_ps(2.443315711809948E-005f);
static const __m128 _ps_coscof_p1 = _mm_set1_ps(-1.388731625493765E-003f);
static const __m128 _ps_coscof_p2 = _mm_set1_ps(4.166664568298827E-002f);
static const __m128 _ps_cephes_FOPI = _mm_set1_ps(1.27323954473516f);

namespace flex
{
	static const real UnitializedMemoryFloat = -431602080.0f;

	static u32 _lastUID = 0;

	GLFWimage LoadGLFWimage(const std::string& filePath, i32 requestedChannelCount, bool bFlipVertically, u32* channelCountOut /* = nullptr */)
	{
		assert(requestedChannelCount == 3 ||
			requestedChannelCount == 4);

		GLFWimage result = {};

		if (g_bEnableLogging_Loading)
		{
			const std::string fileName = StripLeadingDirectories(filePath);
			Print("Loading texture %s\n", fileName.c_str());
		}

		stbi_set_flip_vertically_on_load(bFlipVertically);

		i32 channelCount;
		unsigned char* data = stbi_load(filePath.c_str(),
			&result.width,
			&result.height,
			&channelCount,
			(requestedChannelCount == 4 ? STBI_rgb_alpha : STBI_rgb));

		if (channelCountOut)
		{
			*channelCountOut = (u32)channelCount;
		}

		if (data == 0)
		{
			const char* failureReasonStr = stbi_failure_reason();
			PrintError("Couldn't load image, failure reason: %s, filepath: %s\n", failureReasonStr, filePath.c_str());
			return result;
		}
		else
		{
			assert((u32)result.width <= MAX_TEXTURE_DIM);
			assert((u32)result.height <= MAX_TEXTURE_DIM);

			result.pixels = static_cast<unsigned char*>(data);
		}

		return result;
	}

	void DestroyGLFWimage(GLFWimage& image)
	{
		STBI_FREE(image.pixels);
		image.pixels = nullptr;
	}

	bool HDRImage::Load(const std::string& hdrFilePath, i32 requestedChannelCount, bool bFlipVertically)
	{
		assert(requestedChannelCount == 3 ||
			requestedChannelCount == 4);

		filePath = hdrFilePath;

		if (g_bEnableLogging_Loading)
		{
			const std::string fileName = StripLeadingDirectories(hdrFilePath);
			Print("Loading HDR texture %s\n", fileName.c_str());
		}

		stbi_set_flip_vertically_on_load(bFlipVertically);

		i32 tempW, tempH, tempC;
		pixels = stbi_loadf(filePath.c_str(),
			&tempW,
			&tempH,
			&tempC,
			(requestedChannelCount == 4 ? STBI_rgb_alpha : STBI_rgb));

		width = (u32)tempW;
		height = (u32)tempH;

		channelCount = 4;

		if (!pixels)
		{
			PrintError("Failed to load HDR image at %s\n", filePath.c_str());
			return false;
		}

		assert(width <= MAX_TEXTURE_DIM);
		assert(height <= MAX_TEXTURE_DIM);

		return true;
	}

	void HDRImage::Free()
	{
		STBI_FREE(pixels);
		pixels = nullptr;
	}

	std::string FloatToString(real f, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
		std::stringstream stream;
		stream << std::fixed << std::setprecision(precision) << f;
		return stream.str();
	}

	std::string BoolToString(bool b)
	{
		return b ? "true" : "false";
	}

	std::string IntToString(i32 i, u16 minChars/* = 0 */, char pad /* = '0' */)
	{
		std::string result = std::to_string(glm::abs(i));

		if (i < 0)
		{
			if (result.length() < minChars)
			{
				result = "-" + std::string(minChars - result.length(), pad) + result;
			}
			else
			{
				result = "-" + result;
			}
		}
		else
		{
			if (result.length() < minChars)
			{
				result = std::string(minChars - result.length(), pad) + result;
			}
		}

		return result;
	}

	// Screen-space constructor
	TextCache::TextCache(const std::string& str, AnchorPoint anchor, const glm::vec2& pos,
		const glm::vec4& color, real xSpacing, real scale) :
		str(str),
		anchor(anchor),
		pos(pos.x, pos.y, -1.0f),
		rot(QUAT_IDENTITY),
		color(color),
		xSpacing(xSpacing),
		scale(scale)
	{
	}

	// World-space constructor
	TextCache::TextCache(const std::string& str, const glm::vec3& pos, const glm::quat& rot,
		const glm::vec4& color, real xSpacing, real scale) :
		str(str),
		anchor(AnchorPoint::_NONE),
		pos(pos),
		rot(rot),
		color(color),
		xSpacing(xSpacing),
		scale(scale)
	{
	}

	bool FileExists(const std::string& filePath)
	{
		FILE* file = fopen(filePath.c_str(), "r");

		if (file)
		{
			fclose(file);
			return true;
		}

		return false;
	}

	bool ReadFile(const std::string& filePath, std::string& outFileContents, bool bBinaryFile)
	{
		std::ios::openmode fileMode = std::ios::in;
		if (bBinaryFile)
		{
			fileMode |= std::ios::binary;
		}
		std::ifstream file(filePath.c_str(), fileMode);

		if (!file)
		{
			PrintError("Unable to read file: %s\n", filePath.c_str());
			return false;
		}

		file.ignore(std::numeric_limits<std::streamsize>::max());
		std::streampos fileLen = file.gcount();
		file.clear(); // Clear eof flag
		file.seekg(0, std::ios::beg);

		if ((size_t)fileLen > 0)
		{
			outFileContents.resize((size_t)fileLen);

			file.seekg(0, std::ios::beg);
			file.read(&outFileContents[0], fileLen);
			file.close();

			// Remove extra null terminators caused by Windows line endings
			for (i32 charIndex = 0; charIndex < (i32)outFileContents.size() - 1; ++charIndex)
			{
				if (outFileContents[charIndex] == '\0')
				{
					outFileContents = outFileContents.substr(0, charIndex);
				}
			}
		}

		return true;
	}

	bool ReadFile(const std::string& filePath, std::vector<char>& vec, bool bBinaryFile)
	{
		std::ios::openmode fileMode = std::ios::in | std::ios::ate;
		if (bBinaryFile)
		{
			fileMode |= std::ios::binary;
		}
		std::ifstream file(filePath.c_str(), fileMode);

		if (!file)
		{
			PrintError("Unable to read file: %s\n", filePath.c_str());
			return false;
		}

		std::streampos length = file.tellg();

		vec.resize((size_t)length);

		file.seekg(0, std::ios::beg);
		file.read(vec.data(), length);
		file.close();

		return true;
	}

	bool WriteFile(const std::string& filePath, const std::string& fileContents, bool bBinaryFile)
	{
		std::vector<char> vec(fileContents.begin(), fileContents.end());
		return WriteFile(filePath, vec, bBinaryFile);
	}

	bool WriteFile(const std::string& filePath, const std::vector<char>& vec, bool bBinaryFile)
	{
		std::ios::openmode fileMode = std::ios::out | std::ios::trunc;
		if (bBinaryFile)
		{
			fileMode |= std::ios::binary;
		}
		std::ofstream fileStream(filePath, fileMode);

		if (fileStream.is_open())
		{
			fileStream.write(&vec[0], vec.size());
			fileStream.close();

			return true;
		}

		return false;
	}

	bool OpenJSONFileDialog(const std::string& windowTitle, const std::string& absoluteDirectory, std::string& outSelectedAbsFilePath)
	{
		char filter[] = "JSON files\0*.json\0\0";
		return Platform::OpenFileDialog(windowTitle, absoluteDirectory, outSelectedAbsFilePath, filter);
	}

	std::string StripLeadingDirectories(const std::string& filePath)
	{
		size_t finalSlash = filePath.rfind('/');
		if (finalSlash != std::string::npos)
		{
			return filePath.substr(finalSlash + 1);
		}
		return filePath;
	}

	std::string ExtractDirectoryString(const std::string& filePath)
	{
		// TODO: When no trailing slash exists check if final token is directory
		size_t finalSlash = filePath.rfind('/');
		if (finalSlash != std::string::npos && finalSlash != filePath.length() - 1)
		{
			return filePath.substr(0, finalSlash + 1);
		}
		return filePath;
	}

	std::string StripFileType(const std::string& filePath)
	{
		if (filePath.find('.') != std::string::npos)
		{
			return Split(filePath, '.')[0];
		}
		return filePath;
	}

	std::string ExtractFileType(const std::string& filePath)
	{
		const size_t dotPos = filePath.find_last_of('.');
		if (dotPos != std::string::npos)
		{
			return filePath.substr(dotPos + 1);
		}
		return "";
	}

	bool ParseWAVFile(const std::string& filePath, i32* format, u8** data, i32* size, i32* freq)
	{
		std::vector<char> dataArray;
		if (!ReadFile(filePath, dataArray, true))
		{
			PrintError("Failed to parse WAV file: %s\n", filePath.c_str());
			return false;
		}

		if (dataArray.size() < 12)
		{
			PrintError("Invalid WAV file: %s\n", filePath.c_str());
			return false;
		}

		u32 dataIndex = 0;
		std::string chunkID(&dataArray[dataIndex], 4); dataIndex += 4;

		u32 chunkSize = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		std::string waveID(&dataArray[dataIndex], 4); dataIndex += 4;
		std::string subChunk1ID(&dataArray[dataIndex], 4); dataIndex += 4;

		if (chunkID.compare("RIFF") != 0 ||
			waveID.compare("WAVE") != 0 ||
			subChunk1ID.compare("fmt ") != 0)
		{
			PrintError("Invalid WAVE file header: %s\n", filePath.c_str());
			return false;
		}

		u32 subChunk1Size = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		if (subChunk1Size != 16)
		{
			PrintError("Non-16 bit chunk size in WAVE files in unsupported: %s\n", filePath.c_str());
			return false;
		}

		u16 audioFormat = Parse16u(&dataArray[dataIndex]); dataIndex += 2;
		if (audioFormat != 1) // WAVE_FORMAT_PCM
		{
			PrintError("WAVE file uses unsupported format (only PCM is allowed): %s\n", filePath.c_str());
			return false;
		}

		u16 channelCount = Parse16u(&dataArray[dataIndex]); dataIndex += 2;
		u32 samplesPerSec = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		u32 avgBytesPerSec = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		u16 blockAlign = Parse16u(&dataArray[dataIndex]); dataIndex += 2;
		u16 bitsPerSample = Parse16u(&dataArray[dataIndex]); dataIndex += 2;

		std::string subChunk2ID(&dataArray[dataIndex], 4); dataIndex += 4;
		if (subChunk2ID.compare("data") != 0)
		{
			PrintError("Invalid WAVE file: %s\n", filePath.c_str());
			return false;
		}

		u32 subChunk2Size = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		*data = new u8[subChunk2Size];
		for (u32 i = 0; i < subChunk2Size; ++i)
		{
			(*data)[i] = dataArray[dataIndex];
			++dataIndex;
		}

		constexpr bool bPrintWavStats = false;
		if (bPrintWavStats)
		{
			const std::string fileName = StripLeadingDirectories(filePath);
			Print("Stats about WAV file: %s:\n\tchannel count: %u, samples/s: %u, average bytes/s: %u"
				", block align: %u, bits/sample: %u, chunk size: %u, sub chunk2 ID: \"%s\", sub chunk 2 size: %u\n",
				fileName.c_str(),
				channelCount,
				samplesPerSec,
				avgBytesPerSec,
				blockAlign,
				bitsPerSample,
				chunkSize,
				subChunk2ID.c_str(),
				subChunk2Size);
		}

		switch (channelCount)
		{
		case 1:
		{
			switch (bitsPerSample)
			{
			case 8:
				*format = AL_FORMAT_MONO8;
				break;
			case 16:
				*format = AL_FORMAT_MONO16;
				break;
			default:
				PrintError("WAVE file contains invalid bitsPerSample (must be 8 or 16): %u\n", bitsPerSample);
				break;
			}
		} break;
		case 2:
		{
			switch (bitsPerSample)
			{
			case 8:
				*format = AL_FORMAT_STEREO8;
				break;
			case 16:
				*format = AL_FORMAT_STEREO16;
				break;
			default:
				PrintError("WAVE file contains invalid bitsPerSample (must be 8 or 16): %u\n", bitsPerSample);
				break;
			}
		} break;
		default:
		{
			PrintError("WAVE file contains invalid channel count (must be 1 or 2): %u\n", channelCount);
		} break;
		}

		*size = subChunk2Size;
		*freq = samplesPerSec;

		return true;
	}

	std::string Trim(const std::string& str)
	{
		if (str.empty())
		{
			return str;
		}

		auto iter = str.begin();
		auto riter = str.end() - 1;
		while (iter != riter && isspace(*iter))
		{
			++iter;
		}

		while (riter != iter && isspace(*riter))
		{
			--riter;
		}

		return std::string(iter, riter + 1);
	}

	std::string TrimLeadingWhitespace(const std::string& str)
	{
		if (str.empty())
		{
			return str;
		}

		auto iter = str.begin();
		auto riter = str.end() - 1;
		while (iter != riter && isspace(*iter))
		{
			++iter;
		}

		return std::string(iter, str.end());
	}

	std::string TrimTrailingWhitespace(const std::string& str)
	{
		if (str.empty())
		{
			return str;
		}

		auto iter = str.begin();
		auto riter = str.end() - 1;
		while (riter != iter && isspace(*riter))
		{
			--riter;
		}

		return std::string(iter, riter + 1);
	}

	std::vector<std::string> Split(const std::string& str, char delim)
	{
		std::vector<std::string> result;
		size_t i = 0;

		size_t strLen = str.size();
		while (i != strLen)
		{
			while (i != strLen && str[i] == delim)
			{
				++i;
			}

			size_t j = i;
			while (j != strLen && str[j] != delim)
			{
				++j;
			}

			if (i != j)
			{
				result.push_back(str.substr(i, j - i));
				i = j;
			}
		}

		return result;
	}

	std::vector<std::string> SplitNoStrip(const std::string& str, char delim)
	{
		std::vector<std::string> result;
		size_t i = 0;
		size_t j = 0;

		size_t strLen = str.size();
		while (i != strLen)
		{
			while (i != strLen && str[i] != delim)
			{
				++i;
			}

			result.push_back(str.substr(j, i - j));
			++i;
			j = i;
		}

		return result;
	}

	i32 NextNonAlphaNumeric(const std::string& str, i32 offset)
	{
		while (offset < (i32)str.size())
		{
			if (!isdigit(str[offset]) && !isalpha(str[offset]))
			{
				return offset;
			}
			++offset;
		}

		return -1;
	}

	bool NearlyEquals(real a, real b, real threshold)
	{
		return (abs(a - b) < threshold) && (abs(b - a) < threshold);
	}

	bool NearlyEquals(const glm::vec2& a, const glm::vec2& b, real threshold)
	{
		return (abs(a.x - b.x) < threshold) && (abs(b.x - a.x) < threshold) &&
			(abs(a.y - b.y) < threshold) && (abs(b.y - a.y) < threshold);
	}

	bool NearlyEquals(const glm::vec3& a, const glm::vec3& b, real threshold)
	{
		return (abs(a.x - b.x) < threshold) && (abs(b.x - a.x) < threshold) &&
			(abs(a.y - b.y) < threshold) && (abs(b.y - a.y) < threshold) &&
			(abs(a.z - b.z) < threshold) && (abs(b.z - a.z) < threshold);
	}

	bool NearlyEquals(const glm::vec4& a, const glm::vec4& b, real threshold)
	{
		return (abs(a.x - b.x) < threshold) && (abs(b.x - a.x) < threshold) &&
			(abs(a.y - b.y) < threshold) && (abs(b.y - a.y) < threshold) &&
			(abs(a.z - b.z) < threshold) && (abs(b.z - a.z) < threshold) &&
			(abs(a.w - b.w) < threshold) && (abs(b.w - a.w) < threshold);
	}

	bool NearlyEquals(const glm::quat& a, const glm::quat& b, real threshold)
	{
		// TODO: Handle wrap around/double cover
		return (abs(a.x - b.x) < threshold) && (abs(b.x - a.x) < threshold) &&
			(abs(a.y - b.y) < threshold) && (abs(b.y - a.y) < threshold) &&
			(abs(a.z - b.z) < threshold) && (abs(b.z - a.z) < threshold) &&
			(abs(a.w - b.w) < threshold) && (abs(b.w - a.w) < threshold);
	}

	glm::quat MoveTowards(const glm::quat& a, const glm::quat& b, real delta)
	{
		delta = glm::clamp(delta, 0.00001f, 1.0f);
		glm::quat slerpVal = glm::slerp(a, b, delta);
		if (glm::length(a - b) < 0.00001f)
		{
			return b;
		}
		return slerpVal;
	}

	glm::vec3 MoveTowards(const glm::vec3& a, const glm::vec3& b, real delta)
	{
		delta = glm::clamp(delta, 0.00001f, 1.0f);
		glm::vec3 diff = (b - a);
		delta = glm::min(delta, glm::length(diff));
		if (abs(delta) < 0.00001f)
		{
			return b;
		}
		return a + glm::normalize(diff) * delta;
	}

	real MoveTowards(const real& a, const real b, real delta)
	{
		delta = glm::clamp(delta, 0.00001f, 1.0f);
		return a + (b - a) * delta;
	}

	real Lerp(real a, real b, real t)
	{
		return a * (1.0f - t) + b * t;
	}

	glm::vec2 Lerp(const glm::vec2& a, const glm::vec2& b, real t)
	{
		return a * (1.0f - t) + b * t;
	}

	glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, real t)
	{
		return a * (1.0f - t) + b * t;
	}

	glm::vec4 Lerp(const glm::vec4& a, const glm::vec4& b, real t)
	{
		return a * (1.0f - t) + b * t;
	}

	u32 Pack2FloatToU32(real f1, real f2)
	{
#ifdef DEBUG
		if (f1 < 0.0f) PrintWarn("Attempted to pack negative number (%0.5f). Data will be lost\n", f1);
		if (f2 < 0.0f) PrintWarn("Attempted to pack negative number (%0.5f). Data will be lost\n", f2);
		if (f1 > 1.0f) PrintWarn("Attempted to pack number larger than 1.0 (%0.5f). Data will be lost\n", f1);
		if (f2 > 1.0f) PrintWarn("Attempted to pack number larger than 1.0 (%0.5f). Data will be lost\n", f2);
#endif
		u32 u1 = static_cast<u32>(f1 * 65535.0f);
		u32 u2 = static_cast<u32>(f2 * 65535.0f);
		return (u1 << 16) | (u2 & 0xFFFF);
	}

	void UnpackU32To2Float(u32 u1, real* outF1, real* outF2)
	{
		*outF1 = (u1 >> 16) / 65535.0f;
		*outF2 = (u1 & 0xFFFF) / 65535.0f;
	}

	u32 Parse32u(const char* ptr)
	{
		return ((u8)ptr[0]) + ((u8)ptr[1] << 8) + ((u8)ptr[2] << 16) + ((u8)ptr[3] << 24);
	}

	u16 Parse16u(const char* ptr)
	{
		return ((u8)ptr[0]) + ((u8)ptr[1] << 8);
	}

	bool ParseBool(const std::string& intStr)
	{
		return (intStr.compare("true") == 0);
	}

	glm::i32 ParseInt(const std::string& intStr)
	{
		return (i32)atoi(intStr.c_str());
	}

	real ParseFloat(const std::string& floatStr)
	{
		if (floatStr.empty())
		{
			PrintError("Invalid float string (empty)\n");
			return -1.0f;
		}

		return (real)std::atof(floatStr.c_str());
	}

	glm::vec2 ParseVec2(const std::string& vecStr)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if (parts.size() != 2)
		{
			PrintError("Invalid vec2 field: %s\n", vecStr.c_str());
			return glm::vec2(-1);
		}
		else
		{
			glm::vec2 result(
				std::atof(parts[0].c_str()),
				std::atof(parts[1].c_str()));

			return result;
		}
	}

	glm::vec3 ParseVec3(const std::string& vecStr)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if (parts.size() != 3 && parts.size() != 4)
		{
			PrintError("Invalid vec3 field: %s\n", vecStr.c_str());
			return glm::vec3(-1);
		}
		else
		{
			glm::vec3 result(
				std::atof(parts[0].c_str()),
				std::atof(parts[1].c_str()),
				std::atof(parts[2].c_str()));

			return result;
		}
	}

	glm::vec4 ParseVec4(const std::string& vecStr, real defaultW)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if ((parts.size() != 4 && parts.size() != 3) || (defaultW < 0 && parts.size() != 4))
		{
			PrintError("Invalid vec4 field: %s\n", vecStr.c_str());
			return glm::vec4(-1);
		}
		else
		{
			glm::vec4 result;

			if (parts.size() == 4)
			{
				result = glm::vec4(
					std::atof(parts[0].c_str()),
					std::atof(parts[1].c_str()),
					std::atof(parts[2].c_str()),
					std::atof(parts[3].c_str()));
			}
			else
			{
				result = glm::vec4(
					std::atof(parts[0].c_str()),
					std::atof(parts[1].c_str()),
					std::atof(parts[2].c_str()),
					defaultW);
			}

			return result;
		}
	}

	glm::quat ParseQuat(const std::string& quatStr)
	{
		std::vector<std::string> parts = Split(quatStr, ',');
		glm::quat result;
		result.x = (real)std::atof(parts[0].c_str());
		result.y = (real)std::atof(parts[1].c_str());
		result.z = (real)std::atof(parts[2].c_str());
		result.w = (real)std::atof(parts[3].c_str());
		return result;
	}

	u32 CountSetBits(u32 bits)
	{
		u32 LSHIFT = sizeof(u32*) * 8 - 1;
		u32 bitSetCount = 0;
		u32 bitTest = (u32)1 << LSHIFT;
		u32 i;

		for (i = 0; i <= LSHIFT; ++i)
		{
			bitSetCount += ((bits & bitTest) ? 1 : 0);
			bitTest /= 2;
		}

		return bitSetCount;
	}

	bool IsNanOrInf(real val)
	{
		return isnan(val) || isinf(val);
	}

	bool IsNanOrInf(const glm::vec2& vec)
	{
		return (isnan(vec.x) || isnan(vec.y) ||
			isinf(vec.x) || isinf(vec.y));
	}

	bool IsNanOrInf(const glm::vec3& vec)
	{
		return (isnan(vec.x) || isnan(vec.y) || isnan(vec.z) ||
			isinf(vec.x) || isinf(vec.y) || isinf(vec.z));
	}

	bool IsNanOrInf(const glm::vec4& vec)
	{
		return (isnan(vec.x) || isnan(vec.y) || isnan(vec.z) || isnan(vec.w) ||
			isinf(vec.x) || isinf(vec.y) || isinf(vec.z) || isinf(vec.w));
	}

	bool IsNanOrInf(const glm::quat& quat)
	{
		return (isnan(quat.x) || isnan(quat.y) || isnan(quat.z) || isnan(quat.w) ||
			isinf(quat.x) || isinf(quat.y) || isinf(quat.z) || isinf(quat.w));
	}

	real RoundToNearestPowerOfTwo(real num)
	{
		return pow(2.0f, ceil(log(num) / log(2.0f) - 0.5f));
	}

	u32 NextPowerOfTwo(u32 x)
	{
		assert(x != 0);
		x--;
		x |= x >> 1;
		x |= x >> 2;
		x |= x >> 4;
		x |= x >> 8;
		x |= x >> 16;
		return x + 1;
	}

	u64 NextPowerOfTwo(u64 x)
	{
		assert(x != 0);
		x--;
		x |= x >> 1;
		x |= x >> 2;
		x |= x >> 4;
		x |= x >> 8;
		x |= x >> 16;
		x |= x >> 32;
		return x + 1;
	}

	std::string GetIncrementedPostFixedStr(const std::string& namePrefix, const std::string& defaultName)
	{
		if (namePrefix.empty())
		{
			return defaultName;
		}

		i16 numChars;
		i32 numEndingWith = GetNumberEndingWith(namePrefix, numChars);

		if (numEndingWith == -1)
		{
			numEndingWith = 1;
		}

		std::string result = namePrefix.substr(0, namePrefix.size() - numChars) + IntToString(numEndingWith + 1, numChars);
		return result;
	}

	void PadEnd(std::string& str, i32 minLen, char pad)
	{
		if ((i32)str.length() >= minLen)
		{
			return;
		}

		str = str + std::string(minLen - str.length(), pad);
	}

	void PadStart(std::string& str, i32 minLen, char pad)
	{
		if ((i32)str.length() >= minLen)
		{
			return;
		}

		str = std::string(minLen - str.length(), pad) + str;
	}

	std::string VecToString(const glm::vec2& vec, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
		return VecToString(vec.x, vec.y, precision);
	}

	std::string VecToString(const glm::vec3& vec, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
		return VecToString(vec.x, vec.y, vec.z, precision);
	}

	std::string VecToString(const glm::vec4& vec, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
		return VecToString(vec.x, vec.y, vec.z, vec.w, precision);
	}

	std::string Vec2ToString(real* data, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
		return VecToString(*(data + 0), *(data + 1), precision);
	}

	std::string Vec3ToString(real* data, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
		return VecToString(*(data + 0), *(data + 1), *(data + 2), precision);
	}

	std::string Vec4ToString(real* data, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
		return VecToString(*(data + 0), *(data + 1), *(data + 2), *(data + 3), precision);
	}

	std::string VecToString(real x, real y, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
#if DEBUG
		if (IsNanOrInf(x) || IsNanOrInf(y))
		{
			PrintError("Attempted to convert vec2 with NAN or inf components to string! Setting to zero\n");
			return "0,0";
		}

		if (x == UnitializedMemoryFloat || y == UnitializedMemoryFloat)
		{
			PrintError("Attempted to convert vec2 with values corresponding to unintialized memory (%.0f)\n", UnitializedMemoryFloat);
			return "0,0";
		}
#endif

		std::string result(FloatToString(x, precision) + SEPARATOR_STR +
			FloatToString(y, precision));
		return result;
	}

	std::string VecToString(real x, real y, real z, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
#if DEBUG
		if (IsNanOrInf(x) || IsNanOrInf(y) || IsNanOrInf(z))
		{
			PrintError("Attempted to convert vec3 with NAN or inf components to string (%.2f, %.2f, %.2f), setting to zero\n", x, y, z);
			return "0,0,0";
		}

		if (x == UnitializedMemoryFloat || y == UnitializedMemoryFloat || z == UnitializedMemoryFloat)
		{
			PrintError("Attempted to convert vec3 with values corresponding to unintialized memory (%.0f)\n", UnitializedMemoryFloat);
			return "0,0,0";
		}
#endif

		std::string result(FloatToString(x, precision) + SEPARATOR_STR +
			FloatToString(y, precision) + SEPARATOR_STR +
			FloatToString(z, precision));
		return result;
	}

	std::string VecToString(real x, real y, real z, real w, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
#if DEBUG
		if (IsNanOrInf(x) || IsNanOrInf(y) || IsNanOrInf(z) || IsNanOrInf(w))
		{
			PrintError("Attempted to convert vec4 with NAN or inf components to string! Setting to zero\n");
			return "0,0,0,0";
		}

		if (x == UnitializedMemoryFloat || y == UnitializedMemoryFloat || z == UnitializedMemoryFloat || w == UnitializedMemoryFloat)
		{
			PrintError("Attempted to convert vec4 with values corresponding to unintialized memory (%.0f)\n", UnitializedMemoryFloat);
			return "0,0,0,0";
		}
#endif

		std::string result(FloatToString(x, precision) + SEPARATOR_STR +
			FloatToString(y, precision) + SEPARATOR_STR +
			FloatToString(z, precision) + SEPARATOR_STR +
			FloatToString(w, precision));
		return result;
	}

	std::string QuatToString(const glm::quat& quat, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
		return QuatToString(quat.x, quat.y, quat.z, quat.w, precision);
	}

	std::string QuatToString(real* data, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
		return QuatToString(*(data + 0), *(data + 1), *(data + 2), *(data + 3), precision);
	}

	std::string QuatToString(real x, real y, real z, real w, i32 precision /* = DEFAULT_FLOAT_PRECISION */)
	{
#if DEBUG
		if (IsNanOrInf(x) || IsNanOrInf(y) || IsNanOrInf(z) || IsNanOrInf(w))
		{
			PrintError("Attempted to convert quat with NAN or inf components to string! Setting to zero\n");
			return "0,0,0,0";
		}

		if (x == UnitializedMemoryFloat || y == UnitializedMemoryFloat || z == UnitializedMemoryFloat || w == UnitializedMemoryFloat)
		{
			PrintError("Attempted to convert quat with values corresponding to unintialized memory (%.0f)\n", UnitializedMemoryFloat);
			return "0,0,0";
		}
#endif

		std::string result(FloatToString(x, precision) + SEPARATOR_STR +
			FloatToString(y, precision) + SEPARATOR_STR +
			FloatToString(z, precision) + SEPARATOR_STR +
			FloatToString(w, precision));
		return result;
	}

	void CopyVec3ToClipboard(const glm::vec3& vec)
	{
		CopyColorToClipboard(glm::vec4(vec, 1.0f));
	}

	void CopyVec4ToClipboard(const glm::vec4& vec)
	{
		// TODO: Don't use ImGui for clipboard management since we might want to disable ImGui but still use the clipboard
		ImGui::LogToClipboard();

		ImGui::LogText("%.2ff,%.2ff,%.2ff,%.2ff", vec.x, vec.y, vec.z, vec.w);

		ImGui::LogFinish();
	}

	void CopyColorToClipboard(const glm::vec3& col)
	{
		CopyVec4ToClipboard(glm::vec4(col, 1.0f));
	}

	void CopyColorToClipboard(const glm::vec4& col)
	{
		CopyVec4ToClipboard(col);
	}

	void CopyTransformToClipboard(Transform* transform)
	{
		ImGui::LogToClipboard();

		glm::vec3 pos = transform->GetWorldPosition();
		glm::vec3 rot = glm::eulerAngles(transform->GetWorldRotation());
		glm::vec3 scale = transform->GetWorldScale();
		ImGui::LogText("%.2ff,%.2ff,%.2ff,%.2ff,%.2ff,%.2ff,%.2ff,%.2ff,%.2ff",
			pos.x, pos.y, pos.z, rot.x, rot.y, rot.z, scale.x, scale.y, scale.z);

		ImGui::LogFinish();
	}

	bool PasteTransformFromClipboard(Transform* transform)
	{
		std::string clipboardText = ImGui::GetClipboardText();

		if (clipboardText.empty())
		{
			PrintError("Attempted to paste transform from empty clipboard!\n");
			return false;
		}

		std::vector<std::string> clipboardParts = Split(clipboardText, ',');
		if (clipboardParts.size() != 9)
		{
			PrintError("Attempted to paste transform from clipboard but it doesn't contain a valid transform object! Contents: %s\n", clipboardText.c_str());
			return false;
		}

		transform->SetWorldPosition(glm::vec3(stof(clipboardParts[0]), stof(clipboardParts[1]), stof(clipboardParts[2])), false);
		transform->SetWorldRotation(glm::vec3(stof(clipboardParts[3]), stof(clipboardParts[4]), stof(clipboardParts[5])), false);
		transform->SetWorldScale(glm::vec3(stof(clipboardParts[6]), stof(clipboardParts[7]), stof(clipboardParts[8])), true);

		return true;
	}

	glm::vec3 PasteColor3FromClipboard()
	{
		glm::vec4 color4 = PasteColor4FromClipboard();

		return glm::vec3(color4);
	}

	glm::vec4 PasteColor4FromClipboard()
	{
		const std::string clipboardContents = ImGui::GetClipboardText();

		const size_t comma1 = clipboardContents.find(',');
		const size_t comma2 = clipboardContents.find(',', comma1 + 1);
		const size_t comma3 = clipboardContents.find(',', comma2 + 1);

		if (comma1 == std::string::npos ||
			comma2 == std::string::npos ||
			comma3 == std::string::npos)
		{
			// Clipboard doesn't contain correctly formatted color!
			return VEC4_ZERO;
		}

		glm::vec4 result(
			stof(clipboardContents.substr(0, comma1)),
			stof(clipboardContents.substr(comma1 + 1, comma2 - comma1 - 1)),
			stof(clipboardContents.substr(comma2 + 1, comma3 - comma2 - 1)),
			stof(clipboardContents.substr(comma3 + 1))
		);

		return result;
	}

	std::string& ToLower(std::string& str)
	{
		for (char& c : str)
		{
			c = (char)tolower(c);
		}
		return str;
	}

	char* ToLower(char* str)
	{
		for (u32 i = 0; i < strlen(str); ++i)
		{
			str[i] = (char)tolower(str[i]);
		}
		return str;
	}

	std::string& ToUpper(std::string& str)
	{
		for (char& c : str)
		{
			c = (char)toupper(c);
		}
		return str;
	}

	bool StartsWith(const std::string& str, const std::string& start)
	{
		if (str.length() < start.length())
		{
			return false;
		}

		bool result = (str.substr(0, start.length()).compare(start) == 0);
		return result;
	}

	bool EndsWith(const std::string& str, const std::string& end)
	{
		if (str.length() < end.length())
		{
			return false;
		}

		bool result = (str.substr(str.length() - end.length()).compare(end) == 0);
		return result;
	}

	i32 GetNumberEndingWith(const std::string& str, i16& outNumNumericalChars)
	{
		if (str.empty())
		{
			outNumNumericalChars = 0;
			return -1;
		}

		i16 strLen = (i16)str.size();

		if (!isdigit(str[strLen - 1]))
		{
			outNumNumericalChars = 0;
			return -1;
		}

		i16 firstDigit = strLen - 1;
		while (firstDigit >= 0 && isdigit(str[firstDigit]))
		{
			firstDigit--;
		}
		firstDigit++;

		i32 num = (i32)atoi(str.substr(firstDigit).c_str());
		outNumNumericalChars = (strLen - firstDigit);

		return num;
	}

	const char* GameObjectTypeToString(GameObjectType type)
	{
		assert(ARRAY_LENGTH(GameObjectTypeStrings) == (i32)GameObjectType::_NONE + 1);

		return GameObjectTypeStrings[(i32)type];
	}

	GameObjectType StringToGameObjectType(const char* gameObjectTypeStr)
	{
		assert(ARRAY_LENGTH(GameObjectTypeStrings) == (i32)GameObjectType::_NONE + 1);

		for (i32 i = 0; i < (i32)GameObjectType::_NONE; ++i)
		{
			if (strcmp(GameObjectTypeStrings[i], gameObjectTypeStr) == 0)
			{
				return (GameObjectType)i;
			}
		}

		return GameObjectType::_NONE;
	}

	std::string ReplaceBackSlashesWithForward(std::string str)
	{
		std::for_each(str.begin(), str.end(), [](char& c)
		{
			if (c == '\\')
			{
				c = '/';
			}
		});
		return str;
	}

	std::string RelativePathToAbsolute(const std::string& relativePath)
	{
		if (relativePath.find(':') != std::string::npos)
		{
			// File must already be absolute if it contains a drive character
			return relativePath;
		}

		size_t nextDoubleDot = relativePath.find("..");

		std::string workingDirectory = FlexEngine::s_CurrentWorkingDirectory;

		std::string strippedFilePath = relativePath;

		while (nextDoubleDot != std::string::npos)
		{
			size_t lastSlash = workingDirectory.find_last_of('/');
			if (lastSlash == std::string::npos)
			{
				PrintWarn("Invalidly formed relative path! %s\n", relativePath.c_str());
				nextDoubleDot = std::string::npos;
			}
			else
			{
				workingDirectory = workingDirectory.substr(0, lastSlash);
				strippedFilePath = strippedFilePath.substr(nextDoubleDot);
				nextDoubleDot = relativePath.find("..", nextDoubleDot + 2);
			}
		}

		strippedFilePath = ReplaceBackSlashesWithForward(strippedFilePath);

		std::string absolutePath = workingDirectory + '/' + strippedFilePath;

		return absolutePath;
	}

	// TODO: Test thoroughly
	std::string Replace(const std::string& str, const std::string& pattern, const std::string& replacement)
	{
		std::string result(str);

		u32 i = 0;

		while (i < result.length())
		{
			size_t findIndex = result.find(pattern.c_str(), i);
			if (findIndex != std::string::npos)
			{
				result.replace(result.begin() + findIndex, result.begin() + findIndex + pattern.length(), replacement.begin(), replacement.end());
				i = (u32)(findIndex + replacement.length());
			}
			else
			{
				break;
			}
		}

		return result;
	}

	std::string Replace(const std::string& str, char pattern, char replacement)
	{
		std::string result(str);

		auto iter = result.begin();

		while (iter != result.end())
		{
			size_t findIndex = result.find(pattern, iter - result.begin());
			if (findIndex != std::string::npos)
			{
				result = result.replace(result.begin() + findIndex, result.begin() + findIndex + 1, 1, replacement);
				iter++;
			}
			else
			{
				break;
			}
		}

		return result;
	}

	i32 RandomInt(i32 min, i32 max)
	{
		// TODO: CLEANUP: FIXME: Don't use rand, for the love of God
		i32 value = rand() % (max - min) + min;
		return value;
	}

	real RandomFloat(real min, real max)
	{
		// TODO: CLEANUP: FIXME: Don't use rand, please
		real randN = rand() / (real)RAND_MAX;
		return randN * (max - min) + min;
	}

	void ByteCountToString(char buf[], u32 bufSize, u32 bytes)
	{
		const char* suffixes[] = { "B", "KB", "MB", "GB", "TB", "PB" };
		u32 s = 0;
		double count = bytes;
		while (count >= 1024 && s < 6)
		{
			s++;
			count /= 1024;
		}

		if (count - floor(count) == 0.0)
		{
			snprintf(buf, bufSize, "%d%s", (int)count, suffixes[s]);
		}
		else
		{
			snprintf(buf, bufSize, "%.1f%s", count, suffixes[s]);
		}
	}

	real MinComponent(const glm::vec2& vec)
	{
		return glm::min(vec.x, vec.y);
	}

	real MinComponent(const glm::vec3& vec)
	{
		return glm::min(vec.x, glm::min(vec.y, vec.z));
	}

	real MinComponent(const glm::vec4& vec)
	{
		return glm::min(vec.x, glm::min(vec.y, glm::min(vec.z, vec.w)));
	}

	real MaxComponent(const glm::vec2& vec)
	{
		return glm::max(vec.x, vec.y);
	}

	real MaxComponent(const glm::vec3& vec)
	{
		return glm::max(vec.x, glm::max(vec.y, vec.z));
	}

	real MaxComponent(const glm::vec4& vec)
	{
		return glm::max(vec.x, glm::max(vec.y, glm::max(vec.z, vec.w)));
	}

	glm::vec2 Floor(const glm::vec2& p)
	{
		return glm::vec2(floor(p.x), floor(p.y));
	}

	glm::vec2 Fract(const glm::vec2& p)
	{
		return glm::vec2(glm::mod(p.x, 1.0f), glm::mod(p.y, 1.0f));
	}

	glm::vec3 Floor(const glm::vec3& p)
	{
		return glm::vec3(floor(p.x), floor(p.y), floor(p.z));
	}

	glm::vec3 Fract(const glm::vec3& p)
	{
		return glm::vec3(glm::mod(p.x, 1.0f), glm::mod(p.y, 1.0f), glm::mod(p.z, 1.0f));
	}

	u32 GenerateUID()
	{
		return ++_lastUID;
	}

	bool Contains(const std::vector<const char*>& vec, const char* val)
	{
		for (u32 i = 0; i < (u32)vec.size(); ++i)
		{
			if (strcmp(vec[i], val) == 0) return true;
		}
		return false;
	}

	bool Contains(const char* arr[], u32 arrLen, const char* val)
	{
		for (u32 i = 0; i < arrLen; ++i)
		{
			if (strcmp(arr[i], val) == 0) return true;
		}
		return false;
	}

	bool Contains(const std::string& str, const std::string& pattern)
	{
		return str.find(pattern) != std::string::npos;
	}

	i32 RoundUp(i32 val, i32 alignment)
	{
		return val + alignment - (val % alignment);
	}

	bool DoImGuiRotationDragFloat3(const char* label, glm::vec3& rotation, glm::vec3& outCleanedRotation)
	{
		glm::vec3 pRot = rotation;

		bool bValueChanged = ImGui::DragFloat3(label, &rotation[0], 0.1f);
		if (ImGui::IsItemClicked(1))
		{
			rotation = VEC3_ZERO;
			bValueChanged = true;
		}

		outCleanedRotation = rotation;

		if ((rotation.y >= 90.0f && pRot.y < 90.0f) ||
			(rotation.y <= -90.0f && pRot.y > 90.0f))
		{
			outCleanedRotation.y = 180.0f - rotation.y;
			rotation.x += 180.0f;
			rotation.z += 180.0f;
		}

		if (rotation.y > 90.0f)
		{
			// Prevents "pop back" when dragging past the 90 deg mark
			outCleanedRotation.y = 180.0f - rotation.y;
		}

		outCleanedRotation.x = rotation.x;
		outCleanedRotation.z = rotation.z;

		return bValueChanged;
	}

	// See https://graphics.pixar.com/library/OrthonormalB/paper.pdf
	void CalculateOrthonormalBasis(const glm::vec3& n, glm::vec3& b1, glm::vec3& b2)
	{
		real sign = copysignf(1.0f, n.z);
		const real a = -1.0f / (sign + n.z);
		const real b = n.x * n.y * a;
		b1 = glm::vec3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
		b2 = glm::vec3(b, sign + n.y * n.y * a, -n.y);
	}

	bool SaveImage(const std::string& absoluteFilePath, ImageFormat imageFormat, i32 width, i32 height, i32 channelCount, u8* data, bool bFlipVertically)
	{
		if (data == nullptr ||
			width == 0 ||
			height == 0 ||
			channelCount == 0 ||
			absoluteFilePath.empty())
		{
			PrintError("Attempted to save invalid image to %s\n", absoluteFilePath.c_str());
			return false;
		}

		bool bResult = false;

		stbi_flip_vertically_on_write(bFlipVertically ? 1 : 0);

		const char* fileNameCstr = absoluteFilePath.c_str();
		switch (imageFormat)
		{
		case ImageFormat::JPG:
		{
			const i32 JPEGQuality = 90;
			if (stbi_write_jpg(fileNameCstr, width, height, channelCount, data, JPEGQuality))
			{
				bResult = true;
			}
		} break;
		case ImageFormat::TGA:
		{
			if (stbi_write_tga(fileNameCstr, width, height, channelCount, data))
			{
				bResult = true;
			}
		} break;
		case ImageFormat::PNG:
		{
			i32 strideInBytes = sizeof(data[0]) * channelCount * width;
			if (stbi_write_png(fileNameCstr, width, height, channelCount, data, strideInBytes))
			{
				bResult = true;
			}
		} break;
		case ImageFormat::BMP:
		{
			if (stbi_write_bmp(fileNameCstr, width, height, channelCount, data))
			{
				bResult = true;
			}
		} break;
		}

		return bResult;
	}

	bool Vec2iCompare::operator()(const glm::vec2i& lhs, const glm::vec2i& rhs) const
	{
		return (lhs.y < rhs.y ? true : lhs.y > rhs.y ? false : lhs.x < rhs.x);
	}

	namespace ImGuiExt
	{
		bool InputUInt(const char* message, u32* v, u32 step /* = 1 */, u32 step_fast /* = 100 */, ImGuiInputTextFlags flags /* = 0 */)
		{
			return ImGui::InputScalar(message, ImGuiDataType_U32, v, (void*)(step > 0 ? &step : NULL), (void*)(step_fast > 0 ? &step_fast : NULL), NULL, flags);
		}

		bool SliderUInt(const char* label, u32* v, u32 v_min, u32 v_max, const char* format /* = NULL */)
		{
			return ImGui::SliderScalar(label, ImGuiDataType_U32, v, &v_min, &v_max, format);
		}

		bool DragUInt(const char* label, u32* v, u32 v_min /* = 0 */, u32 v_max /* = 0 */, const char* format /* = "%d" */)
		{
			return ImGui::DragScalar(label, ImGuiDataType_U32, v, 1.0f, &v_min, &v_max, format);
		}

		bool DragInt16(const char* label, i16* v, i16 v_min /* = 0 */, i16 v_max /* = 0 */, const char* format /* = "%d" */)
		{
			i32 v32 = (i32)*v;
			i32 v_min32 = (i32)v_min;
			i32 v_max32 = (i32)v_max;
			bool bResult = ImGui::DragScalar(label, ImGuiDataType_S32, &v32, 1.0f, &v_min32, &v_max32, format);

			if (bResult)
			{
				*v = (i16)v32;
			}

			return bResult;
		}

		bool ColorEdit3Gamma(const char* label, real* v, ImGuiColorEditFlags flags /* = 0 */)
		{
			glm::vec3 vg = glm::pow(glm::vec3(v[0], v[1], v[2]), glm::vec3(1.0f/2.2f));
			bool bResult = ImGui::ColorEdit3(label, &vg.x, flags);

			if (bResult)
			{
				v[0] = glm::pow(vg.x, 2.2f);
				v[1] = glm::pow(vg.y, 2.2f);
				v[2] = glm::pow(vg.z, 2.2f);
			}

			return bResult;
		}

		bool ColorEdit4Gamma(const char* label, real* v, ImGuiColorEditFlags flags /* = 0 */)
		{
			glm::vec4 vg = glm::pow(glm::vec4(v[0], v[1], v[2], v[3]), glm::vec4(1.0f / 2.2f));
			bool bResult = ImGui::ColorEdit4(label, &vg.x, flags);

			if (bResult)
			{
				v[0] = glm::pow(vg.x, 2.2f);
				v[1] = glm::pow(vg.y, 2.2f);
				v[2] = glm::pow(vg.z, 2.2f);
				v[3] = glm::pow(vg.w, 2.2f);
			}

			return bResult;
		}

	} // namespace ImGuiExt
} // namespace flex

