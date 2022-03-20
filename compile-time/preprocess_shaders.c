#include <getopt.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

enum shader_language {
    LANGUAGE_UNDEFINED = 0,
    LANGUAGE_GLSL,
    LANGUAGE_GLSL_ES
};

static inline const char *skip_space(const char *pos)
{
    for (; *pos && isspace(*pos); pos++)
        ;
    return pos;
}

static inline const char *skip_to_eol(const char *pos)
{
    for (; *pos && *pos != '\n'; pos++)
        ;
    return pos;
}

static inline const char *skip_to_new_line(const char *pos)
{
    pos = skip_to_eol(pos);
    return *pos ? skip_space(pos) : pos;
}

static inline int id_char(char c)
{
    return (isalnum(c) || c == '_');
}

static inline int non_id_char(char c)
{
    return !id_char(c);
}

void preprocess_vert_shader(char *input_name, char *output_name, enum shader_language target)
{
    FILE* f = fopen(input_name, "r");
    if (!f) {
        fprintf(stderr, "Cannot open '%s'.\n", input_name);
        exit(EXIT_FAILURE);            
    }
    
    struct stat st;
    fstat(fileno(f), &st);
    char* buf = (char *)calloc(st.st_size + 1, 1);
    fread(buf, 1, st.st_size, f);
    fclose(f);

    FILE* fout = fopen(output_name, "w+");
    if (!fout) {
        fprintf(stderr, "Cannot create '%s'.\n", output_name);
        exit(EXIT_FAILURE);            
    }
    if (target == LANGUAGE_GLSL) {
        fwrite(buf, 1, st.st_size, fout);        
    } else if (target == LANGUAGE_GLSL_ES) {
        const char *next_line;
        for (const char* pos = buf;;) {
            next_line = skip_to_new_line(pos);
            if (next_line == pos) {
                break;
            }

            if (strncmp(pos, "in ", 3) == 0) {
                // "in" -> "attribute"
                pos += 3;
                fwrite("attribute ", 1, 10, fout);
            } else if (strncmp(pos, "#version ", 9) == 0) {
                // delete line
                pos = next_line;
            } else if (strncmp(pos, "out ", 4) == 0) {
                // "out" -> "varying"
                pos += 4;
                fwrite("varying ", 1, 8, fout);
            }

            fwrite(pos, 1, next_line - pos, fout);
        
            pos = next_line;
        }
    }
    fclose(fout);
    free(buf);
}

void preprocess_frag_shader(char *input_name, char *output_name, enum shader_language target)
{
    FILE* f = fopen(input_name, "r");
    if (!f) {
        fprintf(stderr, "Cannot open '%s'.\n", input_name);
        exit(EXIT_FAILURE);
    }
    
    struct stat st;
    fstat(fileno(f), &st);
    char* buf = (char *)calloc(st.st_size + 1, 1);
    fread(buf, 1, st.st_size, f);
    fclose(f);

    FILE* fout = fopen(output_name, "w+");
    if (!fout) {
        fprintf(stderr, "Cannot create '%s'.\n", output_name);
        exit(EXIT_FAILURE);            
    }

    if (target == LANGUAGE_GLSL) {
        fwrite(buf, 1, st.st_size, fout);        
    } else if (target == LANGUAGE_GLSL_ES) {
        fwrite("precision mediump float;\n", 1, 25, fout);
        
        const char *next_line;
        for (const char* pos = buf;;) {
            next_line = skip_to_new_line(pos);
            if (next_line == pos) {
                break;
            }

            if (strncmp(pos, "//", 2) == 0) {
                // copy as-is
            } else if (strncmp(pos, "#version ", 9) == 0) {
                // delete line
                pos = next_line;
            } else if (strncmp(pos, "precision ", 10) == 0) {
                // delete line
                pos = next_line;
            } else if (strncmp(pos, "in ", 3) == 0) {
                // "in" -> "varying"
                pos += 3;
                fwrite("varying ", 1, 8, fout);
            } else if (strncmp(pos, "layout (location=0) ", 20) == 0) {
                // delete line
                pos = next_line;
            } else {
                while (next_line - pos > 0) {
                    const char *start = pos;
                    for (; start != next_line; start++) {
                        if (strncmp(start, "FragColor", 9) == 0
                            && non_id_char(start[-1]) && non_id_char(start[9])) {
                            fwrite(pos, 1, start - pos, fout);
                            fwrite("gl_", 1, 3, fout);
                            pos = start;
                            start += 9;
                            break;
                        }
                        if (strncmp(start, "texture", 7) == 0
                            && non_id_char(start[-1]) && non_id_char(start[7])) {
                            fwrite(pos, 1, start - pos, fout);
                            fwrite("texture2D", 1, 9, fout);
                            pos = start + 7;
                            start += 7;
                            break;
                        }
                    }
                    fwrite(pos, 1, start - pos, fout);
                    pos = start;
                }
            }
            if (pos != next_line) {
                fwrite(pos, 1, next_line - pos, fout);
                pos = next_line;
            }
        }
    }
    fclose(fout);
    free(buf);
}

void preprocess_shader(char* input_name, char* output_path, int convert_to_glsl_es)
{
    int n = strlen(input_name);
    int output_path_length = strlen(output_path);
    
    char input_vert_name[n + 6];
    strcpy(input_vert_name, input_name);
    strcat(input_vert_name, ".vert");
    int i = n;
    while (i >= 0 && input_vert_name[i] != '/') i--;
    char *vert_file_name = input_vert_name + i + 1;
    
    char output_vert_name[output_path_length + strlen(vert_file_name) + 1];
    strcpy(output_vert_name, output_path);
    strcat(output_vert_name, vert_file_name);
    
    char input_frag_name[n + 6];
    strcpy(input_frag_name, input_name);
    strcat(input_frag_name, ".frag");
    i = n;
    while (i >= 0 && input_frag_name[i] != '/') i--;
    char *frag_file_name = input_frag_name + i + 1;
    
    char output_frag_name[output_path_length + strlen(frag_file_name) + 1];
    strcpy(output_frag_name, output_path);
    strcat(output_frag_name, frag_file_name);
    
    printf("Vertex shader: '%s' -> '%s'.\n", input_vert_name, output_vert_name);
    preprocess_vert_shader(input_vert_name, output_vert_name, convert_to_glsl_es);
    printf("Fragment shader: '%s' -> '%s'.\n", input_frag_name, output_frag_name);
    preprocess_frag_shader(input_frag_name, output_frag_name, convert_to_glsl_es);
}

int main(int argc, char **argv, char **envp)
{
    char* output_path = "./";
    enum shader_language target = LANGUAGE_UNDEFINED;

    int c;
    for (;;) {
        c = getopt(argc, argv, "t:o:");
        if (c == -1)
            break;

        switch (c) {
        case 't':
            if (!strcmp(optarg, "glsl-es")) {
                target = LANGUAGE_GLSL_ES;
            } else if (!strcmp(optarg, "glsl")) {
                target = LANGUAGE_GLSL;
            } else {
                fprintf(stderr, "invalid value for -t option. Valid values are: glsl, glsl-es.\n");
                exit(EXIT_FAILURE);            
            }
            break;
        case 'o':
            output_path = optarg;
            break;
        default:
            fprintf(stderr, "invalid option %x\n", c);
            exit(EXIT_FAILURE);            
        }
    }

    switch (target) {
    case LANGUAGE_GLSL:
        printf("Target language: GLSL\n");
        break;
    case LANGUAGE_GLSL_ES:
        printf("Target language: GLSL ES\n");
        break;
    case LANGUAGE_UNDEFINED:
        fprintf(stderr, "Missing required option -t (valid values are: glsl, glsl-es).\n");
        exit(EXIT_FAILURE);
    }
    for (int i = optind; i < argc; i++) {
        printf("Preprocessing %s\n", argv[i]);
        preprocess_shader(argv[i], output_path, target);
    }
}
