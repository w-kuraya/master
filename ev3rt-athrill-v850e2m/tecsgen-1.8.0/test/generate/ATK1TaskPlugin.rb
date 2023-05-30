#
#  TECS Generator
#      Generator for TOPPERS Embedded Component System
#  
#   Copyright (C) 2008-2011 by TOPPERS Project
#--
#   上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
#   ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
#   変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
#   (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
#       権表示，この利用条件および下記の無保証規定が，そのままの形でソー
#       スコード中に含まれていること．
#   (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
#       用できる形で再配布する場合には，再配布に伴うドキュメント（利用
#       者マニュアルなど）に，上記の著作権表示，この利用条件および下記
#       の無保証規定を掲載すること．
#   (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
#       用できない形で再配布する場合には，次のいずれかの条件を満たすこ
#       と．
#     (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
#         作権表示，この利用条件および下記の無保証規定を掲載すること．
#     (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
#         報告すること．
#   (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
#       害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
#       また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
#       由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
#       免責すること．
#  
#   本ソフトウェアは，無保証で提供されているものである．上記著作権者お
#   よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
#   に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
#   アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
#   の責任を負わない．
#  
#   $Id: GenOpaqueMarshaler.rb 1237 2011-01-29 05:43:02Z okuma-top $
#++

#== celltype プラグインの共通の親クラス
class ATK1TaskPlugin < CelltypePlugin
#@celltype:: Celltype
#@option:: String     :オプション文字列

  include Plugin

  #signature::     Celltype        シグニチャ（インスタンス）
  def initialize( celltype, option )
    @celltype = celltype
    @option = option
  end

  #===  CDL ファイルの生成
  #      typedef, signature, celltype, cell のコードを生成
  #      重複して生成してはならない
  #      すでに生成されている場合は出力しないこと。
  #      もしくは同名の import により、重複を避けること。
  #file::        FILE       生成するファイル
#  def gen_cdl_file file
#  end

  def new_cell cell
    join_list = cell.get_join_list
    a = join_list.get_item( :attrib )
    if a == nil
      Generator.error( "attrib: must be initialized")
    elsif ! a.get_rhs.instance_of? Array
      Generator.error( "attrib: initializer must be in format of '{val1, val2 ...}'")
    end
  end

  #===  受け口関数の本体(C言語)を生成する
  #     このメソッドが未定義の場合、ジェネレータは受け口関数のテンプレートを生成する
  #     このメソッドが定義済みの場合、（テンプレートではなく、変更する必要のない）セルタイプコードを生成する
  #file::           FILE        出力先ファイル
  #b_singleton::    bool        true if singleton
  #ct_name::        Symbol
  #global_ct_name:: string
  #sig_name::       string
  #ep_name::        string
  #func_name::      string
  #func_global_name:: string
  #func_type::      class derived from Type
#  def gen_ep_func_body( file, b_singleton, ct_name, global_ct_name, sig_name, ep_name, func_name, func_global_name, func_type, params )
#  end

  def gen_factory file
    file2 = CFile.open( "#{$gen}/tecsgen.oil", "w" )
    @celltype.get_cell_list.each { |cell|
      if cell.is_generate?
        file2.print "#{cell.get_name}: "
        join = cell.get_join_list.get_item( :attrib )
        delim = ""
        join.get_rhs.each { |initial|
          file2.print "#{delim}#{initial.to_s}"
          delim = ", "
        }
        file2.print "\n"
      end
    }
    file2.close

    # 追記するために AppFile を使う（文字コード変換されない）
    file = AppFile.open( "#{$gen}/tTask_tecsgen.c" )
    file.print "\n/* Generated by OSEKPlugin */\n"
    @celltype.get_cell_list.each { |cell|
      if cell.is_generate?
        name_array = @celltype.get_name_array( cell )
        file.print <<EOT
TASK(#{cell.get_name})
{
    CELLCB *p_cellcb = #{name_array[8]};
    cMain_main();
}

EOT
      end
    }
    file.close

  end

end

