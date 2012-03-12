/*
 * File:    procs.c
 * Purpose: Functions to support Call graphs and procedures
 * Date:    November 1993
 * (C) Cristina Cifuentes
 */

#include <cstring>
#include <cassert>
#include "dcc.h"


/* Static indentation buffer */
static constexpr int indSize=81;          /* size of indentation buffer; max 20 */
static char indentBuf[indSize] =
        "                                                                                ";
// not static, used in icode.cpp at emitGotoLabel
const char *indentStr(int indLevel) // Indentation according to the depth of the statement
{
    return (&indentBuf[indSize-(indLevel*4)-1]);
}


/* Inserts an outEdge at the current callGraph pointer if the newProc does
 * not exist.  */
void CALL_GRAPH::insertArc (ilFunction newProc)
{
    CALL_GRAPH *pcg;


    /* Check if procedure already exists */
    auto res=std::find_if(outEdges.begin(),outEdges.end(),[newProc](CALL_GRAPH *e) {return e->proc==newProc;});
    if(res!=outEdges.end())
        return;
    /* Include new arc */
    pcg = new CALL_GRAPH;
    pcg->proc = newProc;
    outEdges.push_back(pcg);
}


/* Inserts a (caller, callee) arc in the call graph tree. */
bool CALL_GRAPH::insertCallGraph(ilFunction caller, ilFunction callee)
{
    int i;

    if (proc == caller)
    {
        insertArc (callee);
        return true;
    }
    else
    {
        for (i = 0; i < outEdges.size(); i++)
            if (outEdges[i]->insertCallGraph (caller, callee))
                return true;
        return (false);
    }
}

bool CALL_GRAPH::insertCallGraph(Function *caller, ilFunction callee)
{
    auto iter = std::find_if(pProcList.begin(),pProcList.end(),
                             [caller](const Function &f)->bool {return caller==&f;});
    assert(iter!=pProcList.end());
    return insertCallGraph(iter,callee);
}


/* Displays the current node of the call graph, and invokes recursively on
 * the nodes the procedure invokes. */
void CALL_GRAPH::writeNodeCallGraph(int indIdx)
{
    int i;

    printf ("%s%s\n", indentStr(indIdx), proc->name.c_str());
    for (i = 0; i < outEdges.size(); i++)
        outEdges[i]->writeNodeCallGraph (indIdx + 1);
}


/* Writes the header and invokes recursive procedure */
void CALL_GRAPH::write()
{
    printf ("\nCall Graph:\n");
    writeNodeCallGraph (0);
}


/**************************************************************************
 *  Routines to support arguments
 *************************************************************************/

/* Updates the argument table by including the register(s) (ie. lhs of
 * picode) and the actual expression (ie. rhs of picode).
 * Note: register(s) are only included once in the table.   */
void Function::newRegArg(iICODE picode, iICODE ticode)
{
    COND_EXPR *lhs;
    STKFRAME * call_args_stackframe, *target_stackframe;
    ID *id;
    int i, tidx;
    boolT regExist;
    condId type;
    Function * tproc;
    eReg regL, regH;		/* Registers involved in arguments */

    /* Flag ticode as having register arguments */
    tproc = ticode->hl()->call.proc;
    tproc->flg |= REG_ARGS;

    /* Get registers and index into target procedure's local list */
    call_args_stackframe = ticode->hl()->call.args;
    target_stackframe = &tproc->args;
    lhs = picode->hl()->asgn.lhs;
    type = lhs->expr.ident.idType;
    if (type == REGISTER)
    {
        regL = localId.id_arr[lhs->expr.ident.idNode.regiIdx].id.regi;
        if (regL < rAL)
            tidx = tproc->localId.newByteWordReg(TYPE_WORD_SIGN, regL);
        else
            tidx = tproc->localId.newByteWordReg(TYPE_BYTE_SIGN, regL);
    }
    else if (type == LONG_VAR)
    {
        regL = localId.id_arr[lhs->expr.ident.idNode.longIdx].id.longId.l;
        regH = localId.id_arr[lhs->expr.ident.idNode.longIdx].id.longId.h;
        tidx = tproc->localId.newLongReg(TYPE_LONG_SIGN, regH, regL, tproc->Icode.begin() /*0*/);
        //tidx = tproc->localId.newLongReg(TYPE_LONG_SIGN, regH, regL, Icode.begin() /*0*/);
    }

    /* Check if register argument already on the formal argument list */
    regExist = false;
    for(STKSYM &tgt_sym : target_stackframe->sym)
    {
        if (type == REGISTER)
        {
            if ((tgt_sym.regs != NULL) &&
                    (tgt_sym.regs->expr.ident.idNode.regiIdx == tidx))
            {
                regExist = true;
            }
        }
        else if (type == LONG_VAR)
        {
            if ((tgt_sym.regs != NULL) &&
                    (tgt_sym.regs->expr.ident.idNode.longIdx == tidx))
            {
                regExist = true;
            }
        }
        if(regExist == true)
            break;
    }

    /* Do ts (formal arguments) */
    if (regExist == false)
    {
        STKSYM newsym;
        sprintf (newsym.name, "arg%ld", target_stackframe->sym.size());
        if (type == REGISTER)
        {
            if (regL < rAL)
            {
                newsym.type = TYPE_WORD_SIGN;
                newsym.regs = COND_EXPR::idRegIdx(tidx, WORD_REG);
            }
            else
            {
                newsym.type = TYPE_BYTE_SIGN;
                newsym.regs = COND_EXPR::idRegIdx(tidx, BYTE_REG);
            }
            sprintf (tproc->localId.id_arr[tidx].name, "arg%ld", target_stackframe->sym.size());
        }
        else if (type == LONG_VAR)
        {
            newsym.regs = COND_EXPR::idLongIdx (tidx);
            newsym.type = TYPE_LONG_SIGN;
            sprintf (tproc->localId.id_arr[tidx].name, "arg%ld", target_stackframe->sym.size());
            tproc->localId.propLongId (regL, regH,
                        tproc->localId.id_arr[tidx].name);
        }
        target_stackframe->sym.push_back(newsym);
        target_stackframe->numArgs++;
    }

    /* Do ps (actual arguments) */
    STKSYM newsym;
    sprintf (newsym.name, "arg%ld", call_args_stackframe->sym.size());
    newsym.actual = picode->hl()->asgn.rhs;
    newsym.regs = lhs;
    /* Mask off high and low register(s) in picode */
    switch (type) {
        case REGISTER:
            id = &localId.id_arr[lhs->expr.ident.idNode.regiIdx];
            picode->du.def &= maskDuReg[id->id.regi];
            if (id->id.regi < rAL)
                newsym.type = TYPE_WORD_SIGN;
            else
                newsym.type = TYPE_BYTE_SIGN;
            break;
        case LONG_VAR:
            id = &localId.id_arr[lhs->expr.ident.idNode.longIdx];
            picode->du.def &= maskDuReg[id->id.longId.h];
            picode->du.def &= maskDuReg[id->id.longId.l];
            newsym.type = TYPE_LONG_SIGN;
            break;
    }
    call_args_stackframe->sym.push_back(newsym);
    call_args_stackframe->numArgs++;
}


/** Inserts the new expression (ie. the actual parameter) on the argument
 * list.
 * @return TRUE if it was a near call that made use of a segment register.
 *         FALSE elsewhere
*/
bool CallType::newStkArg(COND_EXPR *exp, llIcode opcode, Function * pproc)
{
    uint8_t regi;
    /* Check for far procedure call, in which case, references to segment
         * registers are not be considered another parameter (i.e. they are
         * long references to another segment) */
    if (exp)
    {
        if ((exp->type == IDENTIFIER) && (exp->expr.ident.idType == REGISTER))
        {
            regi =  pproc->localId.id_arr[exp->expr.ident.idNode.regiIdx].id.regi;
            if ((regi >= rES) && (regi <= rDS))
                if (opcode == iCALLF)
                    return false;
                else
                    return true;
        }
    }

    /* Place register argument on the argument list */
    STKSYM newsym;
    newsym.actual = exp;
    args->sym.push_back(newsym);
    args->numArgs++;
    return false;
}


/* Places the actual argument exp in the position given by pos in the
 * argument list of picode.	*/
void CallType::placeStkArg (COND_EXPR *exp, int pos)
{
    args->sym[pos].actual = exp;
    sprintf (args->sym[pos].name, "arg%ld", pos);
}


/* Checks to determine whether the expression (actual argument) has the
 * same type as the given type (from the procedure's formal list).  If not,
 * the actual argument gets modified */
void adjustActArgType (COND_EXPR *exp, hlType forType, Function * pproc)
{
    hlType actType;
    int offset, offL;

    if (exp == NULL)
        return;

    actType = expType (exp, pproc);
    if (((actType == forType) || (exp->type != IDENTIFIER)))
        return;
    switch (forType)
    {
        case TYPE_UNKNOWN: case TYPE_BYTE_SIGN:
        case TYPE_BYTE_UNSIGN: case TYPE_WORD_SIGN:
        case TYPE_WORD_UNSIGN: case TYPE_LONG_SIGN:
        case TYPE_LONG_UNSIGN: case TYPE_RECORD:
            break;

        case TYPE_PTR:
        case TYPE_CONST:
            break;

        case TYPE_STR:
            switch (actType) {
                case TYPE_CONST:
                    /* It's an offset into image where a string is
                                         * found.  Point to the string.	*/
                    offL = exp->expr.ident.idNode.kte.kte;
                    if (prog.fCOM)
                        offset = (pproc->state.r[rDS]<<4) + offL + 0x100;
                    else
                        offset = (pproc->state.r[rDS]<<4) + offL;
                    exp->expr.ident.idNode.strIdx = offset;
                    exp->expr.ident.idType = STRING;
                    break;

                case TYPE_PTR:
                    /* It's a pointer to a char rather than a pointer to
                                         * an integer */
                    /***HERE - modify the type ****/
                    break;

                case TYPE_WORD_SIGN:

                    break;
            } /* eos */
            break;
    }
}


/* Determines whether the formal argument has the same type as the given
 * type (type of the actual argument).  If not, the formal argument is
 * changed its type */
void STKFRAME::adjustForArgType(int numArg_, hlType actType_)
{
    hlType forType;
    STKSYM * psym, * nsym;
    int off, i;

    /* Find stack offset for this argument */
    off = m_minOff;
    for (i = 0; i < numArg_; i++)
    {
        if(i>=sym.size())
        {
            break; //TODO: verify
        }
        off += sym[i].size;
    }

    /* Find formal argument */
    if (numArg_ < sym.size())
    {
        psym = &sym[numArg_];
        i = numArg_;
        //auto iter=std::find_if(sym.begin(),sym.end(),[off](STKSYM &s)->bool {s.off==off;});
        auto iter=std::find_if(sym.begin()+numArg_,sym.end(),[off](STKSYM &s)->bool {s.off==off;});
        if(iter==sym.end()) // symbol not found
            return;
        psym = &(*iter);
    }
    /* If formal argument does not exist, do not create new ones, just
     * ignore actual argument
     */
    else
        return;

    forType = psym->type;
    if (forType != actType_)
    {
        switch (actType_) {
            case TYPE_UNKNOWN: case TYPE_BYTE_SIGN:
            case TYPE_BYTE_UNSIGN: case TYPE_WORD_SIGN:
            case TYPE_WORD_UNSIGN: case TYPE_RECORD:
                break;

            case TYPE_LONG_UNSIGN: case TYPE_LONG_SIGN:
                if ((forType == TYPE_WORD_UNSIGN) ||
                        (forType == TYPE_WORD_SIGN) ||
                        (forType == TYPE_UNKNOWN))
                {
                    /* Merge low and high */
                    psym->type = actType_;
                    psym->size = 4;
                    nsym = psym + 1;
                    sprintf (nsym->macro, "HI");
                    sprintf (psym->macro, "LO");
                    nsym->hasMacro = true;
                    psym->hasMacro = true;
                    sprintf (nsym->name, "%s", psym->name);
                    nsym->invalid = true;
                    numArgs--;
                }
                break;

            case TYPE_PTR:
            case TYPE_CONST:
            case TYPE_STR:
                break;
        } /* eos */
    }
}

