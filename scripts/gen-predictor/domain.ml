open Core
open Ir

let rewrite_rules : (term -> term option) list =
  [(function (* Stage #1 rewriting *)
   (* Please ensure any field inside a struct is rewritten here, in particular the first field 
    * It will otherwise be overwritten bby the first rule in stage 2 *)

      (* user_buf[12:14] -> pkt.ether.type *)
      | Utility (Slice ({v=Id "user_buf";t=_}, 96, 16)) ->
        Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "ether");t=Unknown},
                       "type"))
      (* buf_value[32:36] -> pkt.type *)
      | Utility (Slice ({v=Id "buf_value";t=_}, 256, 32)) ->
        Some (Str_idx ({v=Id "pkt";t=Unknown}, "type"))
      (* user_buf[23:24] -> pkt.protocol *)
      | Utility (Slice ({v=Id "user_buf";t=_}, 184, 8)) ->
        Some (Str_idx ({v=Id "pkt";t=Unknown}, "protocol"))
      (* user_buf[14:15] -> pkt.version_ihl *)
      | Utility (Slice ({v=Id "user_buf";t=_}, 112, 8)) ->
        Some (Str_idx ({v=Id "pkt";t=Unknown}, "version_ihl"))
      (* Src and dest MAC addresses *)
      | Utility (Slice ({v=Id "user_buf";t=_}, 0, 48)) ->
        Some (Str_idx ({v=Id "pkt";t=Unknown}, "src_macaddr"))
      | Utility (Slice ({v=Id "user_buf";t=_}, 48, 48)) ->
        Some (Str_idx ({v=Id "pkt";t=Unknown}, "dst_macaddr"))
      | Utility (Slice({v = Id "user_buf"; t = _},0,8)) ->
        Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "src_mac");t=Unknown},"a"))
      | Utility (Slice({v = Id "user_buf"; t = _},8,8)) ->
      Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "src_mac");t=Unknown},"b"))
      | Utility (Slice({v = Id "user_buf"; t = _},16,8)) ->
      Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "src_mac");t=Unknown},"c"))
      | Utility (Slice({v = Id "user_buf"; t = _},24,8)) ->
      Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "src_mac");t=Unknown},"d"))
      | Utility (Slice({v = Id "user_buf"; t = _},32,8)) ->
      Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "src_mac");t=Unknown},"e"))
      | Utility (Slice({v = Id "user_buf"; t = _},40,8)) ->
      Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "src_mac");t=Unknown},"f"))
      | Utility (Slice({v = Id "user_buf"; t = _},48,8)) ->
      Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "dst_mac");t=Unknown},"a"))
      | Utility (Slice({v = Id "user_buf"; t = _},56,8)) ->
      Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "dst_mac");t=Unknown},"b"))
      | Utility (Slice({v = Id "user_buf"; t = _},64,8)) ->
      Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "dst_mac");t=Unknown},"c"))
      | Utility (Slice({v = Id "user_buf"; t = _},72,8)) ->
      Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "dst_mac");t=Unknown},"d"))
      | Utility (Slice({v = Id "user_buf"; t = _},80,8)) ->
      Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "dst_mac");t=Unknown},"e"))
      | Utility (Slice({v = Id "user_buf"; t = _},88,8)) ->
      Some (Str_idx ({v=Str_idx ({v=Id "pkt";t=Unknown}, "dst_mac");t=Unknown},"f"))
      (* Insert program specific rules here *)
      | _ -> None);

   (function (* Stage #2 rewriting *)
      (* Var[0:x] -> Var *)
      | Utility (Slice ({v = Id var ;t=_}, 0, _)) -> Some (Id var)
      (* Int == x -> x == Int *)
      | Bop (Eq, {v=Int i; t}, x) -> Some (Bop (Eq, x, {v=Int i; t}))
      (* Int < x -> x > Int *)
      | Bop (Lt, {v=Int i; t}, x) -> Some (Bop (Gt, x, {v=Int i; t}))
      | _ -> None);

    (function (* Stage #3 rewriting *)
      (* x == 0 -> Not x *)
      | Bop (Eq, x, {v = Int 0; t = Uint32}) -> Some (Not x)
      | _ -> None);

    (function (* Stage #4 rewriting *)
      (* Not(Not(x)) -> x *)
      | Not ({v = Not x; t=_})-> Some (x.v)
      | _ -> None);
    
   (function (* Stage #5 rewriting *)
      (* pkt.is_IP *)
      | Bop(Eq, 
            {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "ether"); t = Unknown}, "type"); t= Sint16},
            {v = Int 8; t = Sint16})
        -> Some (Str_idx({v = Id "pkt"; t = Unknown}, "is_IP"))
      
      (* pkt.has_IP_options *)
      | Bop(Gt,
            {v = Bop(Bit_and, 
                      {v = Str_idx({v = Id "pkt"; t = Unknown}, "version_ihl"); t= Uint32},{v = Int 15; t = Uint32});
            t = Uint32},
            {v = Int 5; t = Sint32})
        -> Some (Str_idx({v = Id "pkt"; t = Unknown}, "has_IP_options"))
    
      (* pkt.is_TCP *)
      | Bop(Eq,
            {v = Str_idx({v = Id "pkt"; t = Unknown},"protocol"); t = Sint8},
            {v = Int 6; t = Sint8})
        -> Some (Str_idx({v = Id "pkt"; t = Unknown}, "is_TCP"))

      (* pkt.is_UDP *)
      | Bop(Eq,
            {v = Str_idx({v = Id "pkt"; t = Unknown},"protocol"); t = Sint8},
            {v = Int 17; t = Sint8})
        -> Some (Str_idx({v = Id "pkt"; t = Unknown}, "is_UDP"))

      (* pkt.src_mac == pkt.dst_mac *)
      | Bop (And, 
                {v = Not { v = Bop(Sub,
                                    {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "a"); t= Uint32},
                                    {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "a"); t= Uint32}); 
                           t = Uint32};
                 t = Boolean},
                { v = Bop(And,
                          {v = Bop(And,
                                    {v = Bop(And,
                                              {v = Bop(And,
                                                        {v = Not { v = Bop(Sub,
                                                                            {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "b"); t= Uint32},
                                                                            {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "b"); t= Uint32}); 
                                                                  t = Uint32};
                                                        t = Boolean},
                                                        {v = Not { v = Bop(Sub,
                                                                            {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "c"); t= Uint32},
                                                                            {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "c"); t= Uint32}); 
                                                                  t = Uint32};
                                                        t = Boolean});
                                              t = Boolean},
                                              {v = Not { v = Bop(Sub,
                                                                  {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "d"); t= Uint32},
                                                                  {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "d"); t= Uint32}); 
                                                        t = Uint32};
                                              t = Boolean});
                                    t = Boolean},
                                    {v = Not { v = Bop(Sub,
                                                        {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "e"); t= Uint32},
                                                        {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "e"); t= Uint32}); 
                                              t = Uint32};
                                    t = Boolean});
                          t = Boolean},
                          {v = Not { v = Bop(Sub,
                                              {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "f"); t= Uint32},
                                              {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "f"); t= Uint32}); 
                                    t = Uint32};
                          t = Boolean});
                t = Boolean}) 
        -> Some ( Bop (Eq, 
                        {v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown},
                        {v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}))

      (* pkt.src_mac == pkt.dst_mac *) (* Sometimes the order gets changed. Need to figure out why *)
      | Bop (And, 
                { v = Bop(And,
                          {v = Bop(And,
                                    {v = Bop(And,
                                              {v = Bop(And,
                                                        {v = Not { v = Bop(Sub,
                                                                            {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "a"); t= Uint32},
                                                                            {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "a"); t= Uint32}); 
                                                                  t = Uint32};
                                                        t = Boolean},
                                                        {v = Not { v = Bop(Sub,
                                                                            {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "b"); t= Uint32},
                                                                            {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "b"); t= Uint32}); 
                                                                  t = Uint32};
                                                        t = Boolean});
                                              t = Boolean},
                                              {v = Not { v = Bop(Sub,
                                                                  {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "c"); t= Uint32},
                                                                  {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "c"); t= Uint32}); 
                                                        t = Uint32};
                                              t = Boolean});
                                    t = Boolean},
                                    {v = Not { v = Bop(Sub,
                                                        {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "d"); t= Uint32},
                                                        {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "d"); t= Uint32}); 
                                              t = Uint32};
                                    t = Boolean});
                          t = Boolean},
                          {v = Not { v = Bop(Sub,
                                              {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "e"); t= Uint32},
                                              {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "e"); t= Uint32}); 
                                    t = Uint32};
                          t = Boolean});
                t = Boolean},
                {v = Not { v = Bop(Sub,
                                    {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown}, "f"); t= Uint32},
                                    {v = Str_idx({v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}, "f"); t= Uint32}); 
                           t = Uint32};
                 t = Boolean}) 
        -> Some ( Bop (Eq, 
                        {v = Str_idx({v = Id "pkt"; t = Unknown}, "src_mac"); t = Unknown},
                        {v = Str_idx({v = Id "pkt"; t = Unknown}, "dst_mac"); t = Unknown}))
    | _ -> None);
                      
  ]