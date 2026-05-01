#pragma once

int report_to_spring_boot_with_result(const char *audio_path, float duration,
                                       const char *keywords, float confidence,
                                       const char *result_prefix);

int report_to_spring_boot(const char *audio_path, float duration,
                          const char *keywords, float confidence);
