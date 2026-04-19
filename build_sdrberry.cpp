#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/wait.h>

struct CommandResult
{
	int exit_code;
	std::string output;
};

class ProgressBar
{
  public:
	ProgressBar()
	{
		std::cout << "\r";
		std::cout.flush();
	}
	~ProgressBar() noexcept
	{
		std::cout << "\n";
		std::cout.flush();
	}

	void update(double percent, const std::string &label)
	{
		percent = std::clamp(percent, 0.0, 100.0);
		constexpr int width = 40;
		int filled = static_cast<int>(percent / 100.0 * width);

		std::cout << "\r[";
		for (int i = 0; i < width; ++i)
		{
			std::cout << (i < filled ? "=" : "-");
		}
		std::cout << "] " << static_cast<int>(percent) << "% ";
		if (!label.empty())
			std::cout << "| " << label;
		std::cout << "     \r";
		std::cout.flush();
	}

	void spinner(int tick)
	{
		static const char frames[] = "|/-\\";
		std::cout << "\r" << frames[tick % 4] << " Processing...     \r";
		std::cout.flush();
	}
};

std::optional<double> extract_percent(const std::string &line)
{
	for (size_t i = 0; i < line.size(); ++i)
	{
		if (line[i] == '%')
		{
			int j = static_cast<int>(i) - 1;
			while (j >= 0 && std::isdigit(static_cast<unsigned char>(line[j])))
				--j;
			++j;
			if (i - j > 0)
			{
				try
				{
					return std::stod(line.substr(j, i - j));
				}
				catch (...)
				{
				}
			}
		}
	}
	return std::nullopt;
}

CommandResult run_with_progress(const std::string &description, const std::string &cmd)
{
	std::string full_cmd = cmd + " 2>&1";
	FILE *pipe = popen(full_cmd.c_str(), "r");
	if (!pipe)
		throw std::runtime_error("popen failed for: " + cmd);

	ProgressBar bar;
	std::string output;
	char buffer[512];
	int spinner_tick = 0;

	while (std::fgets(buffer, sizeof(buffer), pipe))
	{
		output += buffer;
		std::string line(buffer);
		if (auto pct = extract_percent(line))
		{
			bar.update(*pct, description);
		}
		else
		{
			bar.spinner(spinner_tick++);
		}
	}

	int status = pclose(pipe);
	return {WEXITSTATUS(status), output};
}

void run_step(const std::string &description, const std::string &cmd)
{
	std::cout << "\n>> " << description << "...\n";
	auto res = run_with_progress(description, cmd);
	std::cout << res.output;
	if (res.exit_code != 0)
	{
		throw std::runtime_error(description + " failed (exit code " + std::to_string(res.exit_code) + ")");
	}
	std::cout << "[OK] " << description << " succeeded.\n";
}

class WorkingDirGuard
{
  public:
	explicit WorkingDirGuard(const std::filesystem::path &target)
		: original_path(std::filesystem::current_path())
	{
		std::filesystem::create_directories(target);
		std::filesystem::current_path(target);
	}
	~WorkingDirGuard() noexcept
	{
		try
		{
			std::filesystem::current_path(original_path);
		}
		catch (const std::exception &e)
		{
			std::cerr << "\n[WARN] Failed to restore directory: " << e.what() << '\n';
		}
	}
	WorkingDirGuard(const WorkingDirGuard &) = delete;
	WorkingDirGuard &operator=(const WorkingDirGuard &) = delete;

  private:
	std::filesystem::path original_path;
};

int main()
{
	try
	{
		std::filesystem::path build_root = "sdrberry_build";
		std::filesystem::path repo_path = build_root / "sdrberry";
		std::filesystem::path install_dir = std::filesystem::current_path() / "sdrberry_install";

		std::cout << "[LOC] Starting in: " << std::filesystem::current_path() << "\n\n";

		if (std::filesystem::exists(repo_path / ".git"))
		{
			std::cout << "[REPO] Repository exists. Pulling latest changes...\n";
		}
		else
		{
			std::cout << "[REPO] Cloning public repository...\n";
			{
				WorkingDirGuard build_guard(build_root);
				run_step("Cloning", "git clone --progress https://github.com/sdrberry/sdrberry.git");
			}
		}

		{
			WorkingDirGuard repo_guard(repo_path);

			if (std::filesystem::exists(repo_path / ".git"))
			{
				run_step("Pulling", "git pull");
			}

			run_step("Configuring CMake", "cmake -B build -DCMAKE_BUILD_TYPE=Release");
			run_step("Compiling", "cmake --build build -j$(nproc)");
			run_step("Installing (local)", "cmake --install build --prefix " + install_dir.string());

			std::cout << "\n[DONE] Build & Install complete!\n";
			std::cout << "[OUT] Output: " << install_dir / "bin" << "\n";
		}

		std::cout << "[LOC] Back to original directory: " << std::filesystem::current_path() << '\n';
	}
	catch (const std::exception &e)
	{
		std::cerr << "\n[ERR] Error: " << e.what() << '\n';
		return 1;
	}
	return 0;
}