import os
from setuptools import setup, find_packages

HERE = os.path.dirname(os.path.abspath(__file__))
README = os.path.join(HERE, "README.md")

setup(
    name="infinite-daw",
    version="0.6.0",
    description="Headless GLSL shader compiler, audio-reactive video renderer, and modular synthesizer for Python and AI agents by Naman Soni",
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
    keywords=[
        "glsl", "shaders", "shader-compiler", "headless-rendering", "gpu-rendering",
        "audio-reactive", "music-visualizer", "procedural-audio", "modular-synth",
        "touchdesigner", "shadertoy", "creative-coding", "sound-design",
        "dsp", "raymarching", "generative-art", "mcp-server", "ai-agent"
    ],
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Topic :: Multimedia :: Sound/Audio",
        "Topic :: Multimedia :: Graphics",
        "Topic :: Multimedia :: Video",
        "Topic :: Scientific/Engineering :: Visualization",
        "Topic :: Artistic Software",
    ],
    entry_points={
        "console_scripts": [
            "infinite-mcp=infinite.mcp:main",
            "infinite-init=infinite.setup_cli:main",
        ],
    },
    python_requires=">=3.8",
)
