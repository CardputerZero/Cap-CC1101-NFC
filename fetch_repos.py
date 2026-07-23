import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def load_repos():
    with (ROOT / "repos.json").open(encoding="utf-8") as file:
        return json.load(file)


def repo_path(repo):
    return ROOT / repo["path"]


def is_repo_fetched(repo):
    path = repo_path(repo)
    return path.is_dir() and (path / ".git").is_dir()


def fetch_repo(repo, update_existing=True):
    path = repo_path(repo)
    if not (path / ".git").is_dir():
        subprocess.run(["git", "clone", repo["url"], str(path)], check=True)
    elif update_existing:
        subprocess.run(
            ["git", "-C", str(path), "fetch", "--tags", "--prune"], check=True
        )

    subprocess.run(
        ["git", "-C", str(path), "checkout", repo["branch"]], check=True
    )


def fetch_dependencies(repos=None, update_existing=True):
    for repo in repos or load_repos():
        fetch_repo(repo, update_existing=update_existing)


def ensure_dependencies():
    missing = [repo for repo in load_repos() if not is_repo_fetched(repo)]
    if not missing:
        return

    names = ", ".join(repo["path"] for repo in missing)
    print(f"Cap-CC1101-NFC: fetching missing dependencies: {names}")
    fetch_dependencies(missing, update_existing=False)


if __name__ == "__main__":
    fetch_dependencies()
