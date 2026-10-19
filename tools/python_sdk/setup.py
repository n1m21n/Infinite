from setuptools import setup, find_packages

setup(
    name="infinite-daw",
    version="0.6.0",
    description="Python interface for Infinite: real-time modular audiovisual workstation by Naman Soni",
    long_description=open("README.md").read() if open("README.md", "r") else "",
    long_description_content_type="text/markdown",
    author="Naman Soni",
    author_email="contact@infinitedevices.org",
    url="https://infinitedevices.org",
    project_urls={
        "Source": "https://github.com/n1m21n/infinite",
        "Documentation": "https://infinitedevices.org",
    },
    license="MIT",
    packages=find_packages(),
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Topic :: Multimedia :: Sound/Audio",
        "Topic :: Multimedia :: Graphics",
    ],
    entry_points={
        "console_scripts": [
            "infinite-mcp=infinite.mcp:main",
            "infinite-init=infinite.setup_cli:main",
        ],
    },
    python_requires=">=3.8",
)
