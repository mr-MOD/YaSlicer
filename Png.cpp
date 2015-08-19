#include "Png.h"

#include <png.h>
#include <stdexcept>

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

void WritePng(const std::string& fileName, uint32_t width, uint32_t height, const std::vector<uint8_t>& pixData)
{
	const auto ChannelBitDepth = 8;

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
			color_type = PNG_COLOR_TYPE_GRAY;
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

		png_set_IHDR(png_ptr, info_ptr, width, height,
			ChannelBitDepth, color_type, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

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