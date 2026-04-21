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

void update_repo(const std::string &name, const std::string &git_url,
				 const std::filesystem::path &base_dir, const std::filesystem::path &install_base)
{
	auto repo_path = base_dir / name;
	auto install_path = install_base / name; // Not used at the moment

	if (std::filesystem::exists(repo_path / ".git"))
	{
		std::cout << "\n[REPO] " << name << " exists. Pulling latest changes...\n";
		WorkingDirGuard wd(repo_path);
		run_step("Pulling " + name, "git pull");
	}
	else
	{
		std::cout << "\n[REPO] Cloning " << name << "...\n";
		WorkingDirGuard wd(base_dir);
		run_step("Cloning " + name, "git clone --progress " + git_url + " " + name);
	}

	std::cout << "\n[BUILD] Processing " << name << "...\n";
	WorkingDirGuard wd(repo_path);
	run_step("Configuring " + name, "cmake -B build -DCMAKE_BUILD_TYPE=Release");
	run_step("Compiling " + name, "cmake --build build -j$(nproc)");
	//run_step("Installing " + name, "cmake --install build --prefix " + install_path.string());
	run_step("Installing " + name, "cmake --install build");

	// Remove development-only files after install
	if (std::filesystem::exists(install_path / "include"))
	{
		std::filesystem::remove_all(install_path / "include");
	}
	if (std::filesystem::exists(install_path / "share/pkgconfig"))
	{
		std::filesystem::remove_all(install_path / "share/pkgconfig");
	}

	//std::cout << "[OUT] " << name << " installed to: " << install_path / "bin" << "\n";
	std::cout << "[OUT] " << name << " installed to /usr/local/bin\n";
}

int main()
{
	try
	{
		auto start_dir = std::filesystem::current_path();
		std::filesystem::path build_base = start_dir / "sdrberry_build";
		std::filesystem::path install_base = build_base / "install";

		std::filesystem::create_directories(build_base);
		std::filesystem::create_directories(install_base);

		std::cout << "[LOC] Working directory: " << start_dir << "\n";
		std::cout << "[DIR] Build & Install root: " << build_base << "\n\n";

		update_repo("sdrberry",
					"https://github.com/paulh002/sdrberry.git",
					build_base, install_base);

		update_repo("build_sdrberry",
					"https://github.com/paulh002/build_sdrberry.git",
					build_base, install_base);

		std::cout << "\n[DONE] All repositories updated and installed.\n";
		std::cout << "[LOC] Back to: " << std::filesystem::current_path() << '\n';
	}
	catch (const std::exception &e)
	{
		std::cerr << "\n[ERR] Fatal: " << e.what() << '\n';
		return 1;
	}
	return 0;
}