
extern ScalarInterpReturn : qword
extern hk_ScalarInterp : proto

extern RA_GetInterpResult : qword
extern hk_GetInterpResult : proto

.code
	prehk_ScalarInterp proc
		mov rax, [rsp]
		mov [ScalarInterpReturn], rax
		jmp [hk_ScalarInterp]
	prehk_ScalarInterp endp

	prehk_GetInterpResult proc
		mov rax, [rsp]
		mov [RA_GetInterpResult], rax
		jmp [hk_GetInterpResult]
	prehk_GetInterpResult endp
end

