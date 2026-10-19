import os
from setuptools import setup, find_packages

HERE = os.path.dirname(os.path.abspath(__file__))
README = os.path.join(HERE, "README.md")

setup(
    name="infinite-daw",
    version="0.6.0",
    description="Python interface for Infinite: real-time modular audiovisual workstation by Naman Soni",
    long_description=open(README, encoding="utf-8").read() if os.path.exists(README) else "",
    long_description_content_type="text/markdown",
    author="Naman Soni",
    author_email="contact@infinitedevices.org",
    url="https://infinitedevices.org",
    project_urls={
        "Source": "https://github.com/n1m21n/Infinite",
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
