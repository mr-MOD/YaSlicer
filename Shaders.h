#pragma once

#include "GlContext.h"

const std::string VShader = SHADER
(
	precision mediump float;

	attribute vec3 vPosition;
	uniform mat4 wvp;
	uniform vec2 mirror;
	void main()
	{
		gl_Position = wvp * vec4(vPosition, 1);
		gl_Position.xy = gl_Position.xy * mirror;
	}
);

const std::string FShader = SHADER
(
	precision mediump float;

	void main()
	{
		gl_FragColor = vec4(1);
	}
);

//////////////////////////////////////

const std::string MaskVShader = SHADER
(
	precision mediump float;
	attribute vec3 vPosition;

	uniform vec2 plateSize;
	uniform mat4 wv;
	uniform mat4 wvp;

	varying vec2 texCoord;
	void main()
	{
		gl_Position = wvp * vec4(vPosition, 1);
		texCoord = ((wv * vec4(vPosition, 1)).xy + plateSize * 0.5) / plateSize;
	}
);

const std::string MaskFShader = SHADER
(
	precision mediump float;

	varying vec2 texCoord;
	uniform sampler2D maskTexture;

	void main()
	{
		gl_FragColor = texture2D(maskTexture, texCoord);
	}
);

//////////////////////////////////////

const std::string Filter2DVShader = SHADER
(
	precision mediump float;
	attribute vec2 vPosition;
	varying vec2 texCoord;
	
	void main()
	{
		gl_Position = vec4(vPosition, 0, 1);
		texCoord = (vPosition + vec2(1, 1)) * 0.5;
	}
);

const std::string AxialDilateFShader = SHADER
(
	precision mediump float;

	varying vec2 texCoord;
	uniform vec2 texelSize;
	uniform sampler2D texture;

	void main()
	{
		vec4 color0 = texture2D(texture, texCoord);
		vec4 color1 = texture2D(texture, texCoord - texelSize*vec2(1, 0));
		vec4 color2 = texture2D(texture, texCoord - texelSize*vec2(0, 1));
		vec4 color3 = texture2D(texture, texCoord - texelSize*vec2(1, 1));
		gl_FragColor = max(max(color0, color1),max(color2, color3));
	}
);

const std::string OmniDilateFShader = SHADER
(
	precision mediump float;

	varying vec2 texCoord;
	uniform vec2 texelSize;
	uniform sampler2D texture;
	uniform float kernelSize;
	uniform float scale;

	void main()
	{
		vec4 maxColor = vec4(0);
		vec2 offset = vec2(floor(kernelSize / 2.0));
		for (float dy = 0.0; dy < kernelSize; ++dy)
		{
			for (float dx = 0.0; dx < kernelSize; ++dx)
			{
				maxColor = max(maxColor, texture2D(texture, texCoord + texelSize*(vec2(dx, dy) - offset)));
			}
		}

		gl_FragColor = texture2D(texture, texCoord) + maxColor*scale;
	}
);

const std::string BinarizeFShader = SHADER
(
	precision mediump float;

	varying vec2 texCoord;
	uniform float threshold;
	uniform vec2 texelSize;
	uniform sampler2D texture;

	void main()
	{
		vec4 color = texture2D(texture, texCoord);
		gl_FragColor = color.r > threshold ? vec4(0) : vec4(1);
	}
);

const std::string DifferenceFShader = SHADER
(
	precision mediump float;

	varying vec2 texCoord;
	uniform float threshold;
	uniform vec2 texelSize;
	uniform sampler2D texture;
	uniform sampler2D previousLayerTexture;

	void main()
	{
		vec4 color = texture2D(texture, texCoord) - texture2D(previousLayerTexture, texCoord);
		gl_FragColor = color;
	}
);