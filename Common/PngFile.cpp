#include "PngFile.h"

#include <png.h>
#include <stdexcept>

std::vector<uint32_t> CreateGrayscalePalette()
{
	std::vector<uint32_t> palette(256);
	for (size_t i = 0; i < palette.size(); ++i)
	{
		palette[i] = static_cast<uint32_t>((i << 16) | (i << 8) | i);
	}
	return palette;
}

std::vector<uint8_t> ReadPng(const std::string& fileName, uint32_t& width, uint32_t& height, uint32_t& bitsPerPixel)
{
	FILE* fp = nullptr;
	png_structp png_ptr = nullptr;
	png_infop info_ptr = nullptr;

	try
	{
		unsigned char header[8];    // 8 is the maximum size that can be checked

		/* open file and test for it being a png */
		fp = fopen(fileName.c_str(), "rb");
		if (!fp)
			throw std::runtime_error("PNG file could not be opened for reading");

		fread(header, 1, sizeof(header), fp);
		if (png_sig_cmp(header, 0, 8))
			throw std::runtime_error("File is not recognized as a PNG file");


		/* initialize stuff */
		png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

		if (!png_ptr)
			throw std::runtime_error("png_create_read_struct failed");

		info_ptr = png_create_info_struct(png_ptr);
		if (!info_ptr)
			throw std::runtime_error("png_create_info_struct failed");

		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("Error during init_io");

		png_init_io(png_ptr, fp);
		png_set_sig_bytes(png_ptr, 8);

		png_read_info(png_ptr, info_ptr);

		auto png_width = png_get_image_width(png_ptr, info_ptr);
		auto png_height = png_get_image_height(png_ptr, info_ptr);
		auto color_type = png_get_color_type(png_ptr, info_ptr);
		auto channel_depth = png_get_bit_depth(png_ptr, info_ptr);

		/*auto number_of_passes = */png_set_interlace_handling(png_ptr);
		png_read_update_info(png_ptr, info_ptr);

		/* read file */
		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("Error during read_image");

		auto channels = 0;
		switch (color_type)
		{
		case PNG_COLOR_TYPE_RGB:
			channels = 3;
			break;
		case PNG_COLOR_TYPE_RGBA:
			channels = 4;
			break;
		default:
			throw std::runtime_error("PNG reader: can only read RGB or RGBA files");
		}

		std::vector<uint8_t> data;
		std::vector<uint8_t*> rowPointers;

		const auto pixelRowByteSize = png_width * channel_depth * channels / 8;
		data.resize(pixelRowByteSize * png_height);
		rowPointers.resize(png_height);
		for (auto y = 0u; y < png_height; ++y)
		{
			rowPointers[y] = &data[0] + y * pixelRowByteSize;
		}

		png_read_image(png_ptr, &rowPointers[0]);
		fclose(fp);
		fp = nullptr;

		width = png_width;
		height = png_height;
		bitsPerPixel = channel_depth * channels;
		return data;
	}
	catch (const std::exception&)
	{
		if (fp)
		{
			fclose(fp);
		}

		if (png_ptr || info_ptr)
		{
			png_destroy_write_struct(&png_ptr, &info_ptr);
		}

		throw;
	}
}

void WritePng(const std::string& fileName, uint32_t width, uint32_t height, uint32_t bitsPerChannel,
	const std::vector<uint8_t>& pixData, const std::vector<uint32_t>& palette)
{
	FILE *fp = nullptr;
	png_structp png_ptr = nullptr;
	png_infop info_ptr = nullptr;
	try
	{
		/* create file */
		fp = fopen(fileName.c_str(), "wb");
		if (!fp)
			throw std::runtime_error("Can't create png file");

		/* initialize stuff */
		png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (!png_ptr)
			throw std::runtime_error("png_create_write_struct failed");

		info_ptr = png_create_info_struct(png_ptr);
		if (!info_ptr)
			throw std::runtime_error("png_create_info_struct failed");

		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("Error during init_io");

		png_init_io(png_ptr, fp);

		/* write header */
		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("Error during writing header");

		auto nChannels = pixData.size() / (width * height);
		auto color_type = 0;
		switch (nChannels)
		{
		case 1:
			color_type = palette.empty() ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_PALETTE;
			break;
		case 3:
			color_type = PNG_COLOR_TYPE_RGB;
			break;
		case 4:
			color_type = PNG_COLOR_TYPE_RGBA;
			break;
		default:
			throw std::runtime_error("Can only write 1, 3 or 4 channel PNG");
		}

		if (color_type == PNG_COLOR_TYPE_PALETTE)
		{
			std::vector<png_color> pngPalette(palette.size());
			for (size_t i = 0; i < palette.size(); ++i)
			{
				pngPalette[i].red = palette[i] & 0xFF;
				pngPalette[i].green = (palette[i] >> 8) & 0xFF;
				pngPalette[i].blue = (palette[i] >> 16) & 0xFF;
			}
			png_set_PLTE(png_ptr, info_ptr, &pngPalette[0], static_cast<int>(pngPalette.size()));
		}

		png_set_IHDR(png_ptr, info_ptr, width, height,
			bitsPerChannel, color_type, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
		png_set_filter(png_ptr, 0, PNG_NO_FILTERS);
		png_set_sRGB_gAMA_and_cHRM(png_ptr, info_ptr, PNG_sRGB_INTENT_PERCEPTUAL);

		const auto DefaultCompressionLevel = 1;
		png_set_compression_level(png_ptr, DefaultCompressionLevel);
		// set large buffer to write whole image in single IDAT
		// to workaround Perfactory PNG reader bug.
		png_set_compression_buffer_size(png_ptr, pixData.size()); 
		
		png_write_info(png_ptr, info_ptr);

		/* write bytes */
		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("Error during writing bytes");

		std::vector<const uint8_t*> row_pointers(height);
		for (auto i = 0u; i < height; ++i)
		{
			row_pointers[i] = &pixData[width * nChannels * i];
		}

		png_write_image(png_ptr, const_cast<uint8_t**>(&row_pointers[0]));


		/* end write */
		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("[write_png_file] Error during end of write");

		png_write_end(png_ptr, NULL);
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fp);
		fp = nullptr;
	}
	catch (const std::exception&)
	{
		if (fp)
		{
			fclose(fp);
		}

		if (png_ptr || info_ptr)
		{
			png_destroy_write_struct(&png_ptr, &info_ptr);
		}

		throw;
	}
}