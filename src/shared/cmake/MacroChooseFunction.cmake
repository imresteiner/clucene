
INCLUDE (CheckFunctionExists)

#macro that sets OUTPUT as the value of oneof options (if _CL_HAVE_OPTION exists)
MACRO(CHOOSE_FUNCTION name options)
    STRING(TOUPPER ${name} NAME)
    FOREACH(option ${options})
        IF ( NOT FUNCTION_${NAME} )
            STRING(TOUPPER ${option} OPTION)
            SET( option1 ${option} )
            
            STRING(REGEX MATCH "([_a-zA-Z0-9]*)(.*)" CHECK_OPTIONAL_FUNCTIONS_MATCH ${option} )
            IF ( CMAKE_MATCH_2 STREQUAL "" )
                CHECK_FUNCTION_EXISTS (${option} _CL_HAVE_FUNCTION_${OPTION})
            ELSE ( CMAKE_MATCH_2 STREQUAL "" )
                SET( option ${CMAKE_MATCH_1} )
                STRING(TOUPPER ${option} OPTION)

                CHECK_STDCALL_FUNCTION_EXISTS (${option1} _CL_HAVE_FUNCTION_${OPTION})
            ENDIF ( CMAKE_MATCH_2 STREQUAL "" )

    	    IF ( _CL_HAVE_FUNCTION_${OPTION} )
				IF ( option STREQUAL ${name} )
					#already have it, ignore this...
					SET (FUNCTION_${NAME} "/* undef ${name} ${option} */" )
				ELSE ( option STREQUAL ${name} )
					SET (FUNCTION_${NAME} "#define ${name} ${option}")
				ENDIF ( option STREQUAL ${name} )
    	    ENDIF ( _CL_HAVE_FUNCTION_${OPTION} )
    	ENDIF( NOT FUNCTION_${NAME} )
    ENDFOREACH(option ${options})
    
    IF ( NOT FUNCTION_${NAME} )
        SET (FUNCTION_${NAME} "/* undef ${name} */" )
    ELSE ( NOT FUNCTION_${NAME} )
        SET (HAVE_FUNCTION_${NAME} 1)
    ENDIF ( NOT FUNCTION_${NAME} )
ENDMACRO(CHOOSE_FUNCTION)