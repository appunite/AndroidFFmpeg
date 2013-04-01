/*
 * player.c
 * ffstagefright.h
 * Copyright (c) 2012 Jacek Marchwicki from Appunite.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef FFSTAGEFRIGHT_H_
#define FFSTAGEFRIGHT_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "sync.h"

struct StageFrightData {
	void *data;
	ANativeWindow *window;
	WaitFunc *wait_func;
	int stream_no;
};

void register_stagefright_codec();

#ifdef __cplusplus
}
#endif

#endif /* FFSTAGEFRIGHT_H_ */
