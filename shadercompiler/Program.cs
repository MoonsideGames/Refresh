using System;
using System.IO;
using System.Diagnostics;

partial class Program
{
	struct CompileShaderData
	{
		public string glslPath;
		public string outputDir;
		public bool preserveTemp;
		public bool vulkan;
		public bool d3d11;
		public bool ps5;
	}

	private static void DisplayHelpText()
	{
		Console.WriteLine("Usage: refreshc <path-to-glsl-source | directory-with-glsl-source-files>");
		Console.WriteLine("Options:");
		Console.WriteLine("  --vulkan           Emit shader compatible with the Refresh Vulkan backend");
		Console.WriteLine("  --d3d11            Emit shader compatible with the Refresh D3D11 backend");
		Console.WriteLine("  --ps5              Emit shader compatible with the Refresh PS5 backend");
		Console.WriteLine("  --out dir          Write output file(s) to the directory `dir`");
		Console.WriteLine("  --preserve-temp    Do not delete the temp directory after compilation. Useful for debugging.");
	}

	public static int Main(string[] args)
	{
		if (args.Length == 0)
		{
			DisplayHelpText();
			return 1;
		}

		CompileShaderData data = new CompileShaderData();
		string inputPath = null;

		for (int i = 0; i < args.Length; i += 1)
		{
			switch (args[i])
			{
				case "--vulkan":
					data.vulkan = true;
					break;

				case "--d3d11":
					data.d3d11 = true;
					break;

				case "--ps5":
					data.ps5 = true;
					break;

				case "--out":
					i += 1;
					data.outputDir = args[i];
					break;

				case "--preserve-temp":
					data.preserveTemp = true;
					break;

				default:
					if (inputPath == null)
					{
						inputPath = args[i];
					}
					else
					{
						Console.WriteLine($"refreshc: Unknown parameter {args[i]}");
						return 1;
					}
					break;
			}
		}

		if (!data.vulkan && !data.d3d11 && !data.ps5)
		{
			Console.WriteLine($"refreshc: No Refresh platforms selected!");
			return 1;
		}

#if !PS5
		if (data.ps5)
		{
			Console.WriteLine($"refreshc: `PS5` must be defined in the to target the PS5 backend!");
			return 1;
		}
#endif

		if (data.outputDir == null)
		{
			data.outputDir = Directory.GetCurrentDirectory();
		}
		else if (!Directory.Exists(data.outputDir))
		{
			Console.WriteLine($"refreshc: Output directory {data.outputDir} does not exist");
			return 1;
		}

		if (Directory.Exists(inputPath))
		{
			// Loop over and compile each file in the directory
			string[] files = Directory.GetFiles(inputPath);
			foreach (string file in files)
			{
				Console.WriteLine($"Compiling {file}");
				data.glslPath = file;
				int res = CompileShader(ref data);
				if (res != 0)
				{
					return res;
				}
			}
		}
		else
		{
			if (!File.Exists(inputPath))
			{
				Console.WriteLine($"refreshc: glsl source file or directory ({inputPath}) does not exist");
				return 1;
			}

			data.glslPath = inputPath;
			int res = CompileShader(ref data);
			if (res != 0)
			{
				return res;
			}
		}

		return 0;
	}

	static int CompileShader(ref CompileShaderData data)
	{
		int res = 0;
		string shaderName = Path.GetFileNameWithoutExtension(data.glslPath);
		string shaderType = Path.GetExtension(data.glslPath);

		if (shaderType != ".vert" && shaderType != ".frag" && shaderType != ".comp")
		{
			Console.WriteLine("refreshc: Expected glsl source file with extension '.vert', '.frag', or '.comp'");
			return 1;
		}

		// Create the temp directory, if needed
		string tempDir = Path.Combine(Directory.GetCurrentDirectory(), "temp");
		if (!Directory.Exists(tempDir))
		{
			Directory.CreateDirectory(tempDir);
		}

		// Compile to spirv
		string spirvPath = Path.Combine(tempDir, $"{shaderName}.spv");
		res = CompileGlslToSpirv(data.glslPath, shaderName, spirvPath);
		if (res != 0)
		{
			goto cleanup;
		}

		if (data.d3d11 || data.ps5)
		{
			// Transpile to hlsl
			string hlslPath = Path.Combine(tempDir, $"{shaderName}.hlsl");
			res = TranslateSpirvToHlsl(spirvPath, hlslPath);
			if (res != 0)
			{
				goto cleanup;
			}

			// FIXME: Is there a cross-platform way to compile HLSL to DXBC?

#if PS5
			// Transpile to ps5, if requested
			if (data.ps5)
			{
				res = TranslateHlslToPS5(hlslPath, shaderName, shaderType, tempDir);
				if (res != 0)
				{
					goto cleanup;
				}
			}
#endif
		}

		// Create the output blob file
		string outputFilepath = Path.Combine(data.outputDir, $"{shaderName}{shaderType}.refresh");
		using (FileStream fs = File.Create(outputFilepath))
		{
			using (BinaryWriter writer = new BinaryWriter(fs))
			{
				// Magic
				writer.Write(new char[] { 'R', 'F', 'S', 'H'});

				if (data.vulkan)
				{
					string inputPath = Path.Combine(tempDir, $"{shaderName}.spv");
					WriteShaderBlob(writer, inputPath, 1);
				}

#if PS5
				if (data.ps5)
				{
					string ext = GetPS5ShaderFileExtension();
					string inputPath = Path.Combine(tempDir, $"{shaderName}{ext}");
					WriteShaderBlob(writer, inputPath, 2);
				}
#endif

				if (data.d3d11)
				{
					string inputPath = Path.Combine(tempDir, $"{shaderName}.hlsl");
					WriteShaderBlob(writer, inputPath, 3);
				}
			}
		}

	cleanup:
		// Clean up the temp directory
		if (!data.preserveTemp)
		{
			Directory.Delete(tempDir, true);
		}
		return res;
	}

	static void WriteShaderBlob(BinaryWriter writer, string inputPath, byte backend)
	{
		byte[] shaderBlob = File.ReadAllBytes(inputPath);
		writer.Write(backend); // Corresponds to Refresh_Backend
		writer.Write(shaderBlob.Length);
		writer.Write(shaderBlob);
	}

	static int CompileGlslToSpirv(string glslPath, string shaderName, string outputPath)
	{
		Process glslc = Process.Start(
			"glslc",
			$"\"{glslPath}\" -o \"{outputPath}\""
		);
		glslc.WaitForExit();
		if (glslc.ExitCode != 0)
		{
			Console.WriteLine($"refreshc: Could not compile GLSL code");
			return 1;
		}

		return 0;
	}

	static int TranslateSpirvToHlsl(string spirvPath, string outputPath)
	{
		Process spirvcross = Process.Start(
			"spirv-cross",
			$"\"{spirvPath}\" --hlsl --shader-model 50 --output \"{outputPath}\""
		);
		spirvcross.WaitForExit();
		if (spirvcross.ExitCode != 0)
		{
			Console.WriteLine($"refreshc: Could not translate SPIR-V to HLSL");
			return 1;
		}

		return 0;
	}
}
