#!/usr/bin/env python3
"""infinite discord render & patch maker bot (visuals, music, mp3, video).

what: a discord bot that can generate whole modular patches, synthesize music,
      export 320kbps mp3 files, render mp4 videos, and execute field shaders
      directly inside discord channels.
"""

import asyncio
import json
import os
import shutil
import subprocess
import sys
import tempfile

try:
    import discord
    from discord.ext import commands
except ImportError:
    print("[!] discord.py is required: run 'pip install discord.py'")
    sys.exit(0)

TOKEN = os.getenv("DISCORD_BOT_TOKEN")
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def find_infinite():
    candidates = [
        "/Applications/Infinite.app/Contents/MacOS/Infinite",
        os.path.join(ROOT, "build", "Infinite.app", "Contents", "MacOS", "Infinite"),
        os.path.join(ROOT, "build-check", "Infinite"),
        os.path.join(ROOT, "build", "Infinite"),
        shutil.which("Infinite") or "",
    ]
    for c in candidates:
        if c and os.path.exists(c) and os.access(c, os.X_OK):
            return c
    return None


PRESETS = {
    "ambient": """infinite-patch 1
node 1 Notes Random Note Generator
  id notes
end
node 2 Synths Wavetable
  id synth
end
node 3 Utility Audio Out
  id speaker
end
node 4 Source FieldPixel
  id visuals
  f width 720
  f height 720
  s code p = (uv - 0.5) * aspect; r = length(p); a = atan2(p.y, p.x); col = vec3(0.5 + 0.5 * sin(r * 12.0 - t * 2.0 + a * 3.0)) * vec3(0.9, 0.5, 0.7);
end
node 5 Utility Output
  id out
end
note synth 0 notes
aud speaker 0 synth
cable out 0 visuals
""",

    "acid": """infinite-patch 1
node 1 Notes Arpeggiator
  id arp
end
node 2 Synths FM
  id fm_synth
end
node 3 Utility Audio Out
  id speaker
end
node 4 Source FieldPixel
  id visuals
  f width 720
  f height 720
  s code p = (uv - 0.5) * 16.0; id = floor(p); f = fract(p) - 0.5; d = length(f); border = smoothstep(0.02, 0.06, abs(d - 0.4)); hue = sin(id.x * 1.5 + id.y * 2.3 + t); col = (0.5 + 0.5 * sin(hue + vec3(0.0, 1.0, 2.0))) * border;
end
node 5 Utility Output
  id out
end
note fm_synth 0 arp
aud speaker 0 fm_synth
cable out 0 visuals
""",

    "moire": """infinite-patch 1
node 1 Notes Random Note Generator
  id notes
end
node 2 Synths Wavetable
  id synth
end
node 3 Utility Audio Out
  id speaker
end
node 4 Source FieldPixel
  id visuals
  f width 720
  f height 720
  s code param float freq = 18.0 [2.0, 60.0]; param float speed = 2.5 [0.1, 10.0]; param float rings = 8.0 [1.0, 20.0]; p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5); c1 = vec2(-0.15, 0.0); c2 = vec2(0.15, 0.0); p1 = length(p - c1); p2 = length(p - c2); w1 = sin(p1 * freq * rings - t * speed); w2 = sin(p2 * freq * rings + t * speed); val = 0.5 + 0.25 * (w1 + w2); col = vec3(val, val * 0.7, 1.0 - val);
end
node 5 Utility Output
  id out
end
note synth 0 notes
aud speaker 0 synth
cable out 0 visuals
""",

    "fm": """infinite-patch 1
node 1 Notes Random Note Generator
  id notes
  f rateBeats 0.25
  i rangeLow 48
  i rangeHigh 72
  i maxStep 5
end
node 2 Synths FM
  id fm
  f attack1 20
  f decay1 300
  f sustain1 0.2
  f release1 250
  f ratio1 1.0
  f ratio2 2.0
  f level2 0.6
  f attack2 10
  f decay2 200
  f feedback 0.25
  f volume 0.35
end
node 3 AudioEffects Delay
  id dly
  f timeMs 250
  f feedback 40
  f mix 0.35
end
node 4 AudioEffects Reverb
  id verb
  f size 0.75
  f decay 3.0
  f mix 0.3
end
node 5 Utility Audio Out
  id speaker
end
node 6 Source FieldPixel
  id visuals
  f width 720
  f height 720
  s code p = (uv - 0.5) * aspect; r = length(p); a = atan2(p.y, p.x); col = vec3(0.5 + 0.5 * sin(r * 18.0 - t * 3.0 + a * 4.0)) * vec3(0.95, 0.45, 0.7);
end
node 7 Utility Output
  id out
end
note fm 0 notes
aud dly 0 fm
aud verb 0 dly
aud speaker 0 verb
cable out 0 visuals
"""
}


intents = discord.Intents.default()
intents.message_content = True
bot = commands.Bot(command_prefix="!inf ", intents=intents, help_command=None)


@bot.event
async def on_ready():
    await bot.change_presence(activity=discord.Game(name="!inf help | infinitedevices.org"))
    print(f"logged in as {bot.user.name} ({bot.user.id})")
    print("infinite discord bot ready.")


@bot.command(name="help")
async def help_command(ctx):
    embed = discord.Embed(
        title="infinite bot commands",
        description="render visuals, generate whole modular patches, and export mp3s directly in discord.",
        url="https://infinitedevices.org",
    )
    embed.add_field(
        name="!inf make [ambient | acid | moire | fm | <patch>]",
        value="generates a complete modular patch, synthesizes music, and sends both the `.mp3` audio track, preview visual, and downloadable `.inf` patch file. you can also paste custom patch code or upload a `.inf` file.",
        inline=False,
    )
    embed.add_field(
        name="!inf presets",
        value="lists all built-in sound & visual patch presets (`ambient`, `acid`, `moire`, `fm`).",
        inline=False,
    )
    embed.add_field(
        name="!inf mp3 [code or attached .inf]",
        value="synthesizes audio with infinite's dsp engine and sends a high-bitrate 320kbps `.mp3` playable directly in discord. (alias: `!inf audio`)",
        inline=False,
    )
    embed.add_field(
        name="!inf render <code>",
        value="renders a visual field pixel shader.\nexample: `!inf render col = vec3(uv.x, uv.y, sin(t));`",
        inline=False,
    )
    embed.add_field(
        name="!inf video",
        value="renders a 4-second mp4 video with sound from an attached `.inf` patch file.",
        inline=False,
    )
    embed.set_footer(text="infinite: free open-source audiovisual daw • infinitedevices.org")
    await ctx.send(embed=embed)


@bot.command(name="presets")
async def presets_command(ctx):
    embed = discord.Embed(
        title="infinite presets",
        description="built-in modular presets you can synthesize instantly with `!inf make <name>`:",
        url="https://infinitedevices.org",
    )
    embed.add_field(
        name="fm",
        value="frequency modulation synth with random note generator, attack/decay envelope, stereo delay, and hall reverb.",
        inline=False,
    )
    embed.add_field(
        name="ambient",
        value="generative wavetable pad driven by a random walk note generator with chromatic ripple visuals.",
        inline=False,
    )
    embed.add_field(
        name="acid",
        value="fast arpeggiated fm bassline with dynamic resonant field pixel grid visuals.",
        inline=False,
    )
    embed.add_field(
        name="moire",
        value="slow lush wavetable drone paired with dual-center moire interference visual patterns.",
        inline=False,
    )
    embed.set_footer(text="run !inf make <name> to synthesize any preset")
    await ctx.send(embed=embed)


@bot.command(name="make", aliases=["patch", "generate"])
async def make_command(ctx, *, custom: str = ""):
    """generates a patch, synthesizes music, and exports mp3 + .inf patch."""
    infinite_bin = find_infinite()
    if not infinite_bin:
        await ctx.send("infinite executable not found on server host.")
        return

    custom_clean = custom.strip("`").strip()

    # 1. check for uploaded .inf patch attachment
    if ctx.message.attachments:
        attachment = ctx.message.attachments[0]
        if attachment.filename.endswith((".inf", ".infinite")):
            patch_text = (await attachment.read()).decode("utf-8", errors="replace")
        else:
            await ctx.send("please attach a .inf file or paste patch code.")
            return
    elif custom_clean.startswith("infinite-patch"):
        # user pasted a full raw .inf patch from headless engine
        patch_text = custom_clean
    elif custom_clean.lower() in PRESETS:
        # user picked a preset keyword (ambient, acid, moire)
        patch_text = PRESETS[custom_clean.lower()]
    elif custom_clean:
        # user entered custom field dsp audio code
        single_line_code = custom_clean.replace("\n", " ")
        patch_text = f"""infinite-patch 1
node 1 Notes Note Sequencer
  id seq
end
node 2 Synths Field Synth
  id syn
  s code {single_line_code}
end
node 3 Utility Audio Out
  id speaker
end
note syn 0 seq
aud speaker 0 syn
"""
    else:
        patch_text = PRESETS["ambient"]

    try:
        with tempfile.TemporaryDirectory() as tmpdir:
            patch_path = os.path.join(tmpdir, "patch.inf")
            wav_path = os.path.join(tmpdir, "audio.wav")
            mp3_path = os.path.join(tmpdir, "infinite_render.mp3")
            summary_path = os.path.join(tmpdir, "summary.json")

            with open(patch_path, "w") as f:
                f.write(patch_text)

            status_msg = await ctx.send("building patch and synthesizing audio with headless infinite...")

            # 1. render audio
            cmd_audio = [infinite_bin, "--audio-summary", patch_path, summary_path, "--wav", wav_path, "--duration", "6"]
            proc_audio = await asyncio.create_subprocess_exec(
                *cmd_audio, stdout=subprocess.PIPE, stderr=subprocess.PIPE
            )
            await proc_audio.communicate()

            # 2. render visual frame if Output node exists in patch
            found_img = None
            if "Output" in patch_text:
                cmd_frame = [infinite_bin, "--frame", patch_path, "0", tmpdir]
                proc_frame = await asyncio.create_subprocess_exec(
                    *cmd_frame, stdout=subprocess.PIPE, stderr=subprocess.PIPE
                )
                await proc_frame.communicate()
                for f in os.listdir(tmpdir):
                    if f.endswith(".png"):
                        found_img = os.path.join(tmpdir, f)
                        break

            # 3. transcode wav to 320k mp3
            ffmpeg = shutil.which("ffmpeg")
            if ffmpeg and os.path.exists(wav_path):
                proc_mp3 = await asyncio.create_subprocess_exec(
                    ffmpeg, "-y", "-i", wav_path, "-codec:a", "libmp3lame", "-b:a", "320k", mp3_path,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
                )
                await proc_mp3.communicate()

            name = "custom"
            if custom_clean.lower() in PRESETS:
                name = custom_clean.lower()

            files_to_send = []
            if os.path.exists(mp3_path):
                files_to_send.append(discord.File(mp3_path, filename=f"infinite_{name}.mp3"))
            elif os.path.exists(wav_path):
                files_to_send.append(discord.File(wav_path, filename=f"infinite_{name}.wav"))

            files_to_send.append(discord.File(patch_path, filename=f"{name}.inf"))

            embed = discord.Embed(
                title=f"infinite patch generated: {name}",
                description="full modular patch created and synthesized by infinite's headless engine.\nopen the attached `.inf` file in infinite (macos, windows, linux).",
                url="https://infinitedevices.org",
            )
            if found_img and os.path.exists(found_img):
                img_file = discord.File(found_img, filename="preview.png")
                files_to_send.append(img_file)
                embed.set_image(url="attachment://preview.png")

            embed.set_footer(text="download infinite free at infinitedevices.org • github.com/n1m21n/infinite")

            await status_msg.delete()
            await ctx.send(embed=embed, files=files_to_send)
    except Exception as e:
        await ctx.send(f"error processing patch:\n```\n{e}\n```")


@bot.command(name="mp3", aliases=["audio", "synth"])
async def mp3_command(ctx, *, code: str = ""):
    """synthesizes audio and sends a high-bitrate mp3 file."""
    infinite_bin = find_infinite()
    if not infinite_bin:
        await ctx.send("infinite executable not found on server host.")
        return

    with tempfile.TemporaryDirectory() as tmpdir:
        patch_path = os.path.join(tmpdir, "input.inf")
        wav_path = os.path.join(tmpdir, "audio.wav")
        mp3_path = os.path.join(tmpdir, "infinite_track.mp3")
        summary_path = os.path.join(tmpdir, "summary.json")

        if ctx.message.attachments:
            attachment = ctx.message.attachments[0]
            if attachment.filename.endswith((".inf", ".infinite")):
                await attachment.save(patch_path)
            else:
                await ctx.send("please upload a `.inf` patch file.")
                return
        elif code:
            cleaned_code = code.strip("`").strip().replace("\n", " ")
            patch_content = f"""infinite-patch 1
node 1 Notes Note Sequencer
  id seq
end
node 2 Synths Field Synth
  id syn
  s code {cleaned_code}
end
node 3 Utility Audio Out
  id speaker
end
note syn 0 seq
aud speaker 0 syn
"""
            with open(patch_path, "w") as f:
                f.write(patch_content)
        else:
            patch_content = PRESETS["ambient"]
            with open(patch_path, "w") as f:
                f.write(patch_content)

        status_msg = await ctx.send("synthesizing audio and exporting 320kbps mp3...")

        cmd = [infinite_bin, "--audio-summary", patch_path, summary_path, "--wav", wav_path, "--duration", "6"]
        proc = await asyncio.create_subprocess_exec(
            *cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        await proc.communicate()

        ffmpeg = shutil.which("ffmpeg")
        if ffmpeg and os.path.exists(wav_path):
            proc_mp3 = await asyncio.create_subprocess_exec(
                ffmpeg, "-y", "-i", wav_path, "-codec:a", "libmp3lame", "-b:a", "320k", mp3_path,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )
            await proc_mp3.communicate()

        target_file = mp3_path if os.path.exists(mp3_path) else wav_path
        if os.path.exists(target_file):
            file = discord.File(target_file, filename=os.path.basename(target_file))
            embed = discord.Embed(
                title="rendered mp3 with infinite",
                description="synthesized audio track (320kbps, 6 seconds)",
                url="https://infinitedevices.org",
            )
            embed.set_footer(text="modular audio + visuals daw • infinitedevices.org")
            await status_msg.delete()
            await ctx.send(file=file, embed=embed)
        else:
            await status_msg.edit(content="could not synthesize audio. please check patch wiring.")


@bot.command(name="render")
async def render_command(ctx, *, code: str = ""):
    """renders a field shader or an attached .inf patch."""
    infinite_bin = find_infinite()
    if not infinite_bin:
        await ctx.send("infinite executable not found on server host.")
        return

    with tempfile.TemporaryDirectory() as tmpdir:
        patch_path = os.path.join(tmpdir, "input.inf")

        if ctx.message.attachments:
            attachment = ctx.message.attachments[0]
            if attachment.filename.endswith((".inf", ".infinite")):
                await attachment.save(patch_path)
            else:
                await ctx.send("please upload a .inf patch file or paste field code.")
                return
        elif code:
            cleaned_code = code.strip("`").strip().replace("\n", " ")
            patch_content = f"""infinite-patch 1
node 1 Source FieldPixel
  id fp
  f width 720
  f height 720
  s code {cleaned_code}
end
node 2 Utility Output
  id out
end
cable out 0 fp
"""
            with open(patch_path, "w") as f:
                f.write(patch_content)
        else:
            await ctx.send("usage: `!inf render <field code>` or attach a `.inf` patch file.")
            return

        cmd = [infinite_bin, "--frame", patch_path, "0", tmpdir]
        proc = await asyncio.create_subprocess_exec(
            *cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        stdout, stderr = await proc.communicate()

        output_str = stdout.decode("utf-8", errors="replace").strip()
        last_line = output_str.splitlines()[-1] if output_str else ""

        found_img = None
        try:
            data = json.loads(last_line)
            if data.get("ok") and data.get("files"):
                found_img = data["files"][0]
        except Exception:
            pass

        if not found_img:
            for f in os.listdir(tmpdir):
                if f.endswith(".png"):
                    found_img = os.path.join(tmpdir, f)
                    break

        if found_img and os.path.exists(found_img):
            file = discord.File(found_img, filename="render.png")
            embed = discord.Embed(
                title="rendered with infinite",
                description="real-time audiovisual modular workstation",
                url="https://infinitedevices.org",
            )
            embed.set_image(url="attachment://render.png")
            embed.set_footer(text="download free for macos, windows and linux at infinitedevices.org")
            await ctx.send(file=file, embed=embed)
        else:
            err_msg = last_line
            try:
                data = json.loads(last_line)
                if data.get("errors"):
                    err_msg = data["errors"][0].get("message", last_line)
            except Exception:
                pass
            await ctx.send(f"render error:\n```\n{err_msg}\n```")


@bot.command(name="video")
async def video_command(ctx):
    """renders a 4-second mp4 video with sound from an attached patch."""
    infinite_bin = find_infinite()
    if not infinite_bin:
        await ctx.send("infinite executable not found on server host.")
        return

    if not ctx.message.attachments:
        await ctx.send("please attach a `.inf` patch file to render into video.")
        return

    attachment = ctx.message.attachments[0]
    if not attachment.filename.endswith((".inf", ".infinite")):
        await ctx.send("please upload a `.inf` patch file.")
        return

    with tempfile.TemporaryDirectory() as tmpdir:
        patch_path = os.path.join(tmpdir, "input.inf")
        mp4_path = os.path.join(tmpdir, "video.mp4")
        await attachment.save(patch_path)

        status_msg = await ctx.send("rendering 4-second mp4 video with infinite engine...")

        cmd = [infinite_bin, "--render", patch_path, mp4_path, "--duration", "4", "--fps", "30"]
        proc = await asyncio.create_subprocess_exec(
            *cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        stdout, stderr = await proc.communicate()

        output_str = stdout.decode("utf-8", errors="replace").strip()
        last_line = output_str.splitlines()[-1] if output_str else ""

        if os.path.exists(mp4_path):
            file = discord.File(mp4_path, filename="infinite_video.mp4")
            await status_msg.delete()
            await ctx.send(
                content="rendered with **infinite** (https://infinitedevices.org):",
                file=file
            )
        else:
            err_msg = last_line
            try:
                data = json.loads(last_line)
                if data.get("errors"):
                    err_msg = data["errors"][0].get("message", last_line)
            except Exception:
                pass
            await status_msg.edit(content=f"video render error:\n```\n{err_msg}\n```")


async def start_services():
    port = os.getenv("PORT")
    runner = None
    if port:
        from aiohttp import web
        async def handle_ping(request):
            return web.Response(text="infinite discord bot is online (https://infinitedevices.org)")
        app = web.Application()
        app.router.add_get("/", handle_ping)
        app.router.add_get("/healthz", handle_ping)
        runner = web.AppRunner(app)
        await runner.setup()
        site = web.TCPSite(runner, "0.0.0.0", int(port))
        await site.start()
        print(f"health check web server running on port {port}")

    try:
        await bot.start(TOKEN)
    finally:
        if runner:
            await runner.cleanup()


def main():
    if not TOKEN:
        print("[!] DISCORD_BOT_TOKEN environment variable not set.")
        sys.exit(1)
    asyncio.run(start_services())


if __name__ == "__main__":
    main()
