/*
* Open Chinese Convert
*
* Copyright 2010 BYVoid <byvoid1@gmail.com>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "opencc_utils.h"
#include "opencc_converter.h"
#include "opencc_dictionary.h"

#define OPENCC_SP_SEG_DEFAULT_BUFFER_SIZE 1024

typedef struct
{
	int initialized;
	size_t buffer_size;
	size_t * match_length;
	size_t * min_len;
	size_t * parent;
	size_t * path;
} opencc_sp_seg_buffer;

typedef struct
{
	opencc_sp_seg_buffer sp_seg_buffer;
	opencc_dictionary_t dicts;
} opencc_converter_description;

void sp_seg_buffer_free(opencc_sp_seg_buffer * ossb)
{
	free(ossb->match_length);
	free(ossb->min_len);
	free(ossb->parent);
	free(ossb->path);
}

void sp_seg_set_buffer_size(opencc_sp_seg_buffer * ossb, size_t buffer_size)
{
	if (ossb->initialized == TRUE)
		sp_seg_buffer_free(ossb);
	
	ossb->buffer_size = buffer_size;
	ossb->match_length = (size_t *) malloc((buffer_size + 1) * sizeof(size_t));
	ossb->min_len = (size_t *) malloc(buffer_size * sizeof(size_t));
	ossb->parent = (size_t *) malloc(buffer_size * sizeof(size_t));
	ossb->path = (size_t *) malloc(buffer_size * sizeof(size_t));
	
	ossb->initialized = TRUE;
}

size_t sp_seg(opencc_converter_description * cd, wchar_t ** inbuf, size_t * inbuf_left,
		wchar_t ** outbuf, size_t * outbuf_left, size_t length)
{
	/* 最短路徑分詞 */
	
	/* 對長度爲1時特殊優化 */
	if (length == 1)
	{
		const wchar_t * match_rs = dict_match_longest(cd->dicts, *inbuf, 1);
		
		if (match_rs == NULL)
			**outbuf = **inbuf;
		else
			**outbuf = *match_rs;
		
		(*outbuf) ++,(*outbuf_left) --;
		(*inbuf) ++,(*inbuf_left) --;

		/* 必須保證有一個字符空間 */
		return 1;
	}
	
	/* 設置緩衝區空間 */
	opencc_sp_seg_buffer * ossb = &(cd->sp_seg_buffer);
	size_t buffer_size_need = length + 1;
	if (ossb->initialized == FALSE || ossb->buffer_size < buffer_size_need)
		sp_seg_set_buffer_size(&(cd->sp_seg_buffer), buffer_size_need);
	
	size_t i, j;

	for (i = 0; i <= length; i ++)
		ossb->min_len[i] = INFINITY_INT;
	
	ossb->min_len[0] = ossb->parent[0] = 0;
	
	for (i = 0; i < length; i ++)
	{
		/* 獲取所有匹配長度 */
		dict_get_all_match_lengths(cd->dicts, (*inbuf) + i, ossb->match_length);
		
		if (ossb->match_length[1] != 1)
			ossb->match_length[++ ossb->match_length[0]] = 1;
		
		/* 動態規劃求最短分割路徑 */
		for (j = 1; j <= ossb->match_length[0]; j ++)
		{
			size_t k = ossb->match_length[j];
			ossb->match_length[j] = 0;
			
			if (ossb->min_len[i] + 1 <= ossb->min_len[i + k])
			{
				ossb->min_len[i + k] = ossb->min_len[i] + 1;
				ossb->parent[i + k] = i;
			}
		}
	}
	
	/* 取得最短分割路徑 */
	for (i = length, j = ossb->min_len[length]; i != 0; i = ossb->parent[i])
		ossb->path[--j] = i;
	
	size_t inbuf_left_start = *inbuf_left;
	size_t begin, end;

	/* 根據最短分割路徑轉換 */
	for (i = begin = 0; i < ossb->min_len[length]; i ++)
	{
		end = ossb->path[i];
		
		const wchar_t * match_rs = dict_match_longest(cd->dicts, *inbuf, end - begin);

		if (match_rs == NULL)
		{
			**outbuf = **inbuf;
			(*outbuf) ++, (*outbuf_left) --;
			(*inbuf) ++, (*inbuf_left) --;
		}
		else
		{
			/* 輸出緩衝區剩餘空間小於分詞長度 */
			size_t match_len = wcslen(match_rs);
			if (match_len > *outbuf_left)
				break;
			for (; *match_rs; match_rs ++)
			{
				**outbuf = *match_rs;
				(*outbuf) ++,(*outbuf_left) --;
				(*inbuf) ++,(*inbuf_left) --;
			}
		}
		
		begin = end;
	}
	
	return inbuf_left_start - *inbuf_left;
}

size_t converter_convert(opencc_converter_t cdt, wchar_t ** inbuf, size_t * inbuf_left,
		wchar_t ** outbuf, size_t * outbuf_left)
{
	opencc_converter_description * cd = (opencc_converter_description *) cdt;
	/* 歧義分割最短路徑分詞 */
	size_t i, start, bound;
	const wchar_t * inbuf_start = *inbuf;
	size_t inbuf_left_start = *inbuf_left;
	size_t sp_seg_length;
	
	bound = 0;
	
	for (i = start = 0; inbuf_start[i] && *inbuf_left > 0 && *outbuf_left > 0; i ++)
	{
		if (i != 0 && i == bound)
		{
			/* 對歧義部分進行最短路徑分詞 */
			sp_seg_length = sp_seg(cd, inbuf, inbuf_left, outbuf, outbuf_left, bound - start);
			if (sp_seg_length ==  OPENCC_CONVERT_ERROR)
				return OPENCC_CONVERT_ERROR;
			if (sp_seg_length == 0)
			{
				if (inbuf_left_start - *inbuf_left > 0)
					return inbuf_left_start - *inbuf_left;
				/* 空間不足 */
				return OPENCC_CONVERT_ERROR;
			}
			start = i;
		}
	
		const wchar_t * match_rs = dict_match_longest(cd->dicts, inbuf_start + i, 0);
		
		size_t match_len = 1;
		if (match_rs != NULL)
			match_len = wcslen(match_rs);
		
		if (i + match_len > bound)
			bound = i + match_len;
	}
	
	if (*inbuf_left > 0 && *outbuf_left > 0)
	{
		sp_seg_length = sp_seg(cd, inbuf, inbuf_left, outbuf, outbuf_left, bound - start);
		if (sp_seg_length ==  OPENCC_CONVERT_ERROR)
			return OPENCC_CONVERT_ERROR;
		if (sp_seg_length == 0)
		{
			if (inbuf_left_start - *inbuf_left > 0)
				return inbuf_left_start - *inbuf_left;
			/* 空間不足 */
			return OPENCC_CONVERT_ERROR;
		}
	}

	return inbuf_left_start - *inbuf_left;
}

size_t converter_convert1(opencc_converter_t cdt, wchar_t ** inbuf, size_t * inbuf_left,
		wchar_t ** outbuf, size_t * outbuf_left)
{
	opencc_converter_description * cd = (opencc_converter_description *) cdt;

	/* 正向最大分詞 */
	size_t inbuf_left_start = *inbuf_left;
	wchar_t * ous = *outbuf;

	for (; **inbuf && *inbuf_left > 0 && *outbuf_left > 0;)
	{
		const wchar_t * match_rs = dict_match_longest(cd->dicts, *inbuf, *inbuf_left);

		if (match_rs == NULL)
		{
			**outbuf = **inbuf;
			(*outbuf) ++, (*outbuf_left) --;
			(*inbuf) ++, (*inbuf_left) --;
		}
		else
		{
			/* 輸出緩衝區剩餘空間小於分詞長度 */
			size_t match_len = wcslen(match_rs);
			if (match_len > *outbuf_left)
			{
				if (inbuf_left_start - *inbuf_left > 0)
					break;
				return OPENCC_CONVERT_ERROR;
			}

			for (; *match_rs; match_rs ++)
			{
				**outbuf = *match_rs;
				(*outbuf) ++,(*outbuf_left) --;
				(*inbuf) ++,(*inbuf_left) --;
			}
		}
	}

	return inbuf_left_start - *inbuf_left;
}

void converter_assign_dicts(opencc_converter_t cdt, opencc_dictionary_t dicts)
{
	opencc_converter_description * cd = (opencc_converter_description *) cdt;
	cd->dicts = dicts;
}

opencc_converter_t converter_open()
{
	opencc_converter_description * cd = (opencc_converter_description *)
			malloc(sizeof(opencc_converter_description));

	cd->sp_seg_buffer.initialized = FALSE;
	cd->sp_seg_buffer.buffer_size = OPENCC_SP_SEG_DEFAULT_BUFFER_SIZE;
	cd->sp_seg_buffer.match_length = cd->sp_seg_buffer.min_len
			= cd->sp_seg_buffer.parent = cd->sp_seg_buffer.path = NULL;

	cd->dicts = NULL;

	return (opencc_converter_t) cd;
}

void converter_close(opencc_converter_t cdt)
{
	opencc_converter_description * cd = (opencc_converter_description *) cdt;

	sp_seg_buffer_free(&(cd->sp_seg_buffer));

	free(cd);
}
