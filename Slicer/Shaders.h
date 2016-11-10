#pragma once

#include "GlContext.h"

const std::string VShader = SHADER
(
	precision mediump float;

	attribute vec3 vPosition;
	attribute vec3 vNormal;
	uniform mat4 wvp;
	uniform vec2 mirror;
	uniform float inflate;
	void main()
	{
		gl_Position = wvp * vec4(vPosition + vec3(inflate, inflate, 0) * sign(vNormal), 1);
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

const std::string DifferenceFShader = SHADER
(
	precision mediump float;

	varying vec2 texCoord;
	uniform sampler2D texture;
	uniform sampler2D previousLayerTexture;

	void main()
	{
		vec4 color = texture2D(texture, texCoord) - texture2D(previousLayerTexture, texCoord);
		gl_FragColor = color;
	}
);

const std::string CombineMaxFShader = SHADER
(
	precision mediump float;

	varying vec2 texCoord;
	uniform sampler2D texture;
	uniform sampler2D combineTexture;

	void main()
	{
		vec4 color = max(texture2D(texture, texCoord), texture2D(combineTexture, texCoord));
		gl_FragColor = color;
	}
);